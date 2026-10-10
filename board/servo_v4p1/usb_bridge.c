/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "hooks.h"
#include "link_defs.h"
#include "queue.h"
#include "registers.h"
#include "servo_usb_bridge_frame.h"
#include "task.h"
#include "timer.h"
#include "usart.h"
#include "usb-stream.h"
#include "usb_api.h"
#include "usb_bridge.h"
#include "usb_hw.h"
#include "usb_pd.h"
#include "util.h"

static struct servo_bridge_node bridge;
static uint8_t active_usb_protocol = SERVO_PROTO_NONE;
static uint8_t effective_usb_protocol = SERVO_PROTO_NONE;
static uint8_t prev_effective_proto = SERVO_PROTO_NONE;
static uint8_t last_enumerated_proto = SERVO_PROTO_NONE;
static bool dppu_pulse_active;
static volatile bool ep7_in_drained_irq;
static volatile uint8_t ep7_out_pending;
static volatile bool host_usb_reset_irq;
volatile bool usart4_rx_corrupt_irq;
static size_t ep_bridge_rx_len;
static bool ep7_zlp_in_flight;
static uint8_t fb_tx_64b_count;
static uint32_t last_ep7_in_drain_us;
static bool kbd_in_flight;
static uint32_t last_key_cmd_us;

static usb_uint ep_bridge_rx_buffer[64 / 2] __usb_ram;
static usb_uint ep_bridge_tx_buffer[64 / 2] __usb_ram;

const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_BRIDGE) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_BRIDGE,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = 0x00,
	.bInterfaceProtocol = 0x00,
	.iInterface = USB_STR_BRIDGE_NAME,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_BRIDGE, 0) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | USB_EP_BRIDGE,
	.bmAttributes = 0x02 /* Bulk IN */,
	.wMaxPacketSize = 64,
	.bInterval = 0,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_BRIDGE, 1) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_EP_BRIDGE,
	.bmAttributes = 0x02 /* Bulk OUT */,
	.wMaxPacketSize = 64,
	.bInterval = 0,
};

static void usb_bridge_tx(void);
static void usb_bridge_rx(void);
static void usb_bridge_event(enum usb_ep_event evt);

USB_DECLARE_EP(USB_EP_BRIDGE, usb_bridge_tx, usb_bridge_rx, usb_bridge_event);

static void bridge_arbiter_deferred(void);
DECLARE_DEFERRED(bridge_arbiter_deferred);

static void bridge_dppu_debounce_deferred(void);
DECLARE_DEFERRED(bridge_dppu_debounce_deferred);

static void bridge_dppu_reconnect_deferred(void);
DECLARE_DEFERRED(bridge_dppu_reconnect_deferred);

#define EP_CTR_RX BIT(15)
#define EP_CTR_TX BIT(7)
#define EP_CTR_BOTH (EP_CTR_RX | EP_CTR_TX)

static void ep7_toggle_safe(uint16_t mask, uint16_t val)
{
	interrupt_disable();
	STM32_TOGGLE_EP(USB_EP_BRIDGE, mask, val, EP_CTR_BOTH);
	interrupt_enable();
}

static void ep7_disarm_tx_preserve_dtog(void)
{
	ep7_zlp_in_flight = false;
	btable_ep[USB_EP_BRIDGE].tx_count = 0;
	if (effective_usb_protocol != SERVO_PROTO_NONE &&
	    effective_usb_protocol == last_enumerated_proto &&
	    !dppu_pulse_active) {
		ep7_toggle_safe(EP_TX_MASK, EP_TX_NAK);
	}
}

static void usb_bridge_update_effective_proto(void)
{
	effective_usb_protocol = pd_is_connected(1) ? active_usb_protocol :
						      SERVO_PROTO_NONE;

	/* 1. Transition State Cleanup */
	if (effective_usb_protocol != prev_effective_proto) {
		ep7_out_pending = 0;
		ep_bridge_rx_len = 0;
		ep7_zlp_in_flight = false;
		fb_tx_64b_count = 0;
		prev_effective_proto = effective_usb_protocol;
	}

	/* 2. Disconnected / PD-Unattached */
	if (effective_usb_protocol == SERVO_PROTO_NONE) {
		set_descriptor_patch(USB_DESC_BRIDGE,
				     &usb_desc_iface6_0iface.bInterfaceSubClass,
				     0x0000);
		hook_call_deferred(&bridge_dppu_debounce_deferred_data, -1);
		ep7_toggle_safe(EP_TX_RX_MASK, EP_TX_STALL | EP_RX_STALL);
		return;
	}

	/* 3. Active Protocol (0x01 ADB or 0x03 Fastboot) */
	set_descriptor_patch(
		USB_DESC_BRIDGE, &usb_desc_iface6_0iface.bInterfaceSubClass,
		(effective_usb_protocol == SERVO_PROTO_ADB) ? 0x0142 : 0x0342);

	if (effective_usb_protocol == last_enumerated_proto) {
		hook_call_deferred(&bridge_dppu_debounce_deferred_data, -1);
		if (!dppu_pulse_active) {
			btable_ep[USB_EP_BRIDGE].tx_count = 0;
			btable_ep[USB_EP_BRIDGE].rx_count = usb_ep_rx_size(64);
			ep7_toggle_safe(EP_TX_RX_MASK, EP_TX_NAK | EP_RX_VALID);
		}
	} else {
		ep7_toggle_safe(EP_TX_RX_MASK, EP_TX_STALL | EP_RX_STALL);
		if (!dppu_pulse_active) {
			hook_call_deferred(&bridge_dppu_debounce_deferred_data,
					   150 * MSEC);
		}
	}
}

static void usb_bridge_pd_event(void)
{
	usb_bridge_update_effective_proto();
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT, usb_bridge_pd_event, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, usb_bridge_pd_event, HOOK_PRIO_DEFAULT);

static void bridge_dppu_debounce_deferred(void)
{
	if (effective_usb_protocol != SERVO_PROTO_NONE &&
	    effective_usb_protocol != last_enumerated_proto) {
		dppu_pulse_active = true;
		ep7_toggle_safe(EP_TX_RX_MASK, EP_TX_STALL | EP_RX_STALL);
		usb_disconnect();
		hook_call_deferred(&bridge_dppu_reconnect_deferred_data,
				   50 * MSEC);
	}
}

static void bridge_dppu_reconnect_deferred(void)
{
	dppu_pulse_active = false;
	last_enumerated_proto = effective_usb_protocol;
	usb_connect();
	if (effective_usb_protocol != SERVO_PROTO_NONE) {
		btable_ep[USB_EP_BRIDGE].tx_count = 0;
		btable_ep[USB_EP_BRIDGE].rx_count = usb_ep_rx_size(64);
		ep7_toggle_safe(EP_TX_RX_MASK | EP_RX_DTOG | EP_TX_DTOG,
				EP_TX_NAK | EP_RX_VALID);
	}
}

void usb_bridge_init(void)
{
	ep7_out_pending = 0;
	ep_bridge_rx_len = 0;
	ep7_zlp_in_flight = false;
	fb_tx_64b_count = 0;
	kbd_in_flight = false;

	set_descriptor_patch(USB_DESC_BRIDGE,
			     &usb_desc_iface6_0iface.bInterfaceSubClass,
			     0x0000);

	servo_bridge_init(&bridge, /*is_epoch_leader=*/true,
			  (uint32_t)get_time().val);

	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static void usb_bridge_tx(void)
{
	STM32_TOGGLE_EP(USB_EP_BRIDGE, 0, 0, EP_CTR_RX);
	ep7_in_drained_irq = true;
	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static void usb_bridge_rx(void)
{
	STM32_TOGGLE_EP(USB_EP_BRIDGE, 0, 0, EP_CTR_TX);
	ep7_out_pending = 1;
	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static void usb_bridge_event(enum usb_ep_event evt)
{
	if (evt == USB_EVENT_RESET) {
		btable_ep[USB_EP_BRIDGE].tx_addr =
			usb_sram_addr(ep_bridge_tx_buffer);
		btable_ep[USB_EP_BRIDGE].tx_count = 0;
		btable_ep[USB_EP_BRIDGE].rx_addr =
			usb_sram_addr(ep_bridge_rx_buffer);
		btable_ep[USB_EP_BRIDGE].rx_count = usb_ep_rx_size(64);

		STM32_USB_EP(USB_EP_BRIDGE) =
			USB_EP_BRIDGE |
			((effective_usb_protocol != SERVO_PROTO_NONE &&
			  effective_usb_protocol == last_enumerated_proto &&
			  !dppu_pulse_active) ?
				 (EP_TX_NAK | EP_RX_VALID) :
				 (EP_TX_STALL | EP_RX_STALL));

		host_usb_reset_irq = true;
		hook_call_deferred(&bridge_arbiter_deferred_data, 0);
	}
}

static void usb_bridge_uart_written(struct consumer const *consumer,
				    size_t count)
{
	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static struct consumer_ops const usb_bridge_uart_consumer_ops = {
	.written = usb_bridge_uart_written,
};

static void usb_bridge_written(struct consumer const *consumer, size_t count)
{
	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static void usb_bridge_uart_read(struct producer const *producer, size_t count)
{
	if (queue_is_empty(&bridge_to_usart4))
		hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static void usb_bridge_read(struct producer const *producer, size_t count)
{
	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static struct consumer_ops const usb_bridge_consumer_ops = {
	.written = usb_bridge_written,
};

static struct producer_ops const usb_bridge_uart_producer_ops = {
	.read = usb_bridge_uart_read,
};

static struct producer_ops const usb_bridge_producer_ops = {
	.read = usb_bridge_read,
};

struct consumer const usb_bridge_uart_consumer = {
	.queue = &usart4_to_bridge,
	.ops = &usb_bridge_uart_consumer_ops,
};
struct producer const usb_bridge_uart_producer = {
	.queue = &bridge_to_usart4,
	.ops = &usb_bridge_uart_producer_ops,
};
struct consumer const usb_bridge_kbd_consumer = {
	.queue = &usb_to_bridge_kbd,
	.ops = &usb_bridge_consumer_ops,
};
struct producer const usb_bridge_kbd_producer = {
	.queue = &bridge_kbd_to_usb,
	.ops = &usb_bridge_producer_ops,
};

static void bridge_arbiter_deferred(void)
{
	uint32_t now_us = (uint32_t)get_time().val;
	uint8_t ch;
	uint8_t frame_buf[SERVO_MAX_FRAME_BYTES];
	uint8_t frame_len;

	for (int pass = 0; pass < 4; pass++) {
		/* 1. Hardware Microsecond Timebase & Deferred IRQ Events */
		if (host_usb_reset_irq) {
			host_usb_reset_irq = false;
			ep7_out_pending = 0;
			ep_bridge_rx_len = 0;
			ep7_zlp_in_flight = false;
			fb_tx_64b_count = 0;
			servo_bridge_trigger_reset(&bridge, now_us);
		}

		if (ep7_in_drained_irq) {
			ep7_in_drained_irq = false;
			last_ep7_in_drain_us = now_us;
			if (ep7_zlp_in_flight) {
				ep7_zlp_in_flight = false;
			} else if (bridge.usb_in_busy) {
				servo_bridge_drain_usb_in(&bridge);
			}
		}

		if (usart4_rx_corrupt_irq) {
			interrupt_disable();
			usart4_rx_corrupt_irq = false;
			queue_init(&usart4_to_bridge);
			interrupt_enable();
			servo_frame_rx_init(&bridge.rx);
			servo_bridge_on_corrupt_rx(&bridge);
		}

		/* 2. UART RX Parsing & State Machine Dispatch */
		if (!queue_is_empty(&usart4_to_bridge) &&
		    bridge.rx.state != SERVO_RX_WAIT_SYNC) {
			bridge.rx.frame_start_us = now_us;
		}

		while (queue_remove_unit(&usart4_to_bridge, &ch)) {
			struct servo_bridge_rx_effects fx;
			enum servo_rx_event evt =
				servo_frame_rx_step(&bridge.rx, ch, now_us);

			if (evt != SERVO_RX_EVT_NONE ||
			    bridge.rx.timeout_occurred) {
				servo_bridge_on_rx_event(&bridge, evt, &fx);

				if (fx.proto_changed) {
					active_usb_protocol =
						bridge.active_proto;
					usb_bridge_update_effective_proto();
				}

				if (fx.epoch_history_cleared) {
					ep7_out_pending = 0;
					ep_bridge_rx_len = 0;
					ep7_zlp_in_flight = false;
					fb_tx_64b_count = 0;
					if (effective_usb_protocol !=
						    SERVO_PROTO_NONE &&
					    effective_usb_protocol ==
						    last_enumerated_proto &&
					    !dppu_pulse_active) {
						btable_ep[USB_EP_BRIDGE]
							.tx_count = 0;
						btable_ep[USB_EP_BRIDGE]
							.rx_count =
							usb_ep_rx_size(64);
						ep7_toggle_safe(
							EP_TX_RX_MASK,
							EP_TX_NAK |
								EP_RX_VALID);
					}
				}

				if (fx.usb_in_armed &&
				    servo_adb_session_filter_rx_data(&bridge,
								     &fx)) {
					if (effective_usb_protocol !=
						    SERVO_PROTO_NONE &&
					    effective_usb_protocol ==
						    last_enumerated_proto &&
					    !dppu_pulse_active) {
						ep7_zlp_in_flight = false;
						memcpy_to_usbram(
							(void *)usb_sram_addr(
								ep_bridge_tx_buffer),
							fx.usb_in_payload,
							fx.usb_in_len);
						btable_ep[USB_EP_BRIDGE]
							.tx_count =
							fx.usb_in_len;
						ep7_toggle_safe(EP_TX_MASK,
								EP_TX_VALID);
						if (bridge.active_proto ==
						    SERVO_PROTO_FASTBOOT) {
							fb_tx_64b_count =
								(fx.usb_in_len ==
								 64) ?
									((fb_tx_64b_count +
									  1) &
									 0x03) :
									0;
						}
					} else {
						servo_bridge_drain_usb_in(
							&bridge);
					}
				}

				if (fx.key_ack_received) {
					kbd_in_flight = false;
					queue_add_unit(&bridge_kbd_to_usb,
						       &fx.key_ack_ch);
				}

				if (evt == SERVO_RX_EVT_FRAME_COMPLETE &&
				    (bridge.rx.type & SERVO_OPCODE_MASK) ==
					    SERVO_FRAME_RESET_REQ &&
				    (bridge.rx.arg_len &
				     SERVO_RESET_FLAG_BOOT) != 0) {
					kbd_in_flight = false;
				}
			}
		}

		/*
		 * 3. RX Timeout, TX Retransmission, kbd_in_flight, & Fastboot
		 *    EP7 IN Idle-ZLP Checks.
		 */
		servo_bridge_check_timers(&bridge, now_us);

		if (kbd_in_flight &&
		    (uint32_t)(now_us - last_key_cmd_us) >= 50000U)
			kbd_in_flight = false;

		if (bridge.active_proto == SERVO_PROTO_FASTBOOT &&
		    (fb_tx_64b_count & 0x03) != 0 && !bridge.usb_in_busy &&
		    !ep7_zlp_in_flight &&
		    bridge.rx.state == SERVO_RX_WAIT_SYNC &&
		    queue_is_empty(&usart4_to_bridge) &&
		    (uint32_t)(now_us - last_ep7_in_drain_us) >= 5000U) {
			fb_tx_64b_count = 0;
			ep7_zlp_in_flight = true;
			btable_ep[USB_EP_BRIDGE].tx_count = 0;
			ep7_toggle_safe(EP_TX_MASK, EP_TX_VALID);
		}

		/* 4. Inbound Keyboard & Bulk OUT Staging */
		if (kbd_in_flight && !bridge.pending_key_cmd &&
		    queue_count(&usb_to_bridge_kbd) > 0)
			kbd_in_flight = false;

		if (servo_bridge_can_send_key_cmd(&bridge, kbd_in_flight) &&
		    queue_count(&usb_to_bridge_kbd) > 0) {
			uint8_t kch;
			if (queue_remove_unit(&usb_to_bridge_kbd, &kch)) {
				servo_bridge_send_key_cmd(&bridge, kch);
				kbd_in_flight = true;
				last_key_cmd_us = now_us;
			}
		}

		if (ep7_out_pending && servo_bridge_can_send_data(&bridge)) {
			uint8_t pkt[SERVO_MAX_DATA_LEN];
			ep_bridge_rx_len = btable_ep[USB_EP_BRIDGE].rx_count &
					   RX_COUNT_MASK;
			if (ep_bridge_rx_len > SERVO_MAX_DATA_LEN)
				ep_bridge_rx_len = SERVO_MAX_DATA_LEN;
			if (ep_bridge_rx_len > 0) {
				memcpy_from_usbram(pkt,
						   (void *)usb_sram_addr(
							   ep_bridge_rx_buffer),
						   ep_bridge_rx_len);
			}
			if (servo_is_adb_cnxn(pkt, (uint8_t)ep_bridge_rx_len,
					      bridge.active_proto)) {
				if (servo_adb_session_on_host_cnxn(&bridge))
					ep7_disarm_tx_preserve_dtog();
			}
			servo_bridge_send_data(&bridge, pkt, ep_bridge_rx_len);
			ep7_out_pending = 0;
			btable_ep[USB_EP_BRIDGE].rx_count = usb_ep_rx_size(64);
			ep7_toggle_safe(EP_RX_MASK, EP_RX_VALID);
		}

		/* 5. Packet Arbiter TX Scheduling */
		while ((frame_len = servo_bridge_pop_tx_frame(
				&bridge, queue_space(&bridge_to_usart4),
				frame_buf, now_us)) > 0) {
			queue_add_units(&bridge_to_usart4, frame_buf,
					frame_len);
		}

		if (queue_is_empty(&usart4_to_bridge) && !ep7_in_drained_irq &&
		    !host_usb_reset_irq && !usart4_rx_corrupt_irq)
			break;

		now_us = (uint32_t)get_time().val;
	}

	/* 6. High-Precision Re-Arming */
	if (!queue_is_empty(&usart4_to_bridge) || ep7_in_drained_irq ||
	    host_usb_reset_irq || usart4_rx_corrupt_irq) {
		hook_call_deferred(&bridge_arbiter_deferred_data, 0);
	} else if (bridge.rx.state != SERVO_RX_WAIT_SYNC ||
		   bridge.tx_credits == 0 ||
		   bridge.state == SERVO_STATE_RESETTING || kbd_in_flight ||
		   bridge.pending_reset_req || bridge.pending_reset_ack ||
		   bridge.pending_credit || bridge.pending_nak ||
		   bridge.pending_key_cmd || bridge.pending_data_tx ||
		   ep7_out_pending ||
		   (bridge.active_proto == SERVO_PROTO_FASTBOOT &&
		    (fb_tx_64b_count & 0x03) != 0 && !ep7_zlp_in_flight)) {
		hook_call_deferred(&bridge_arbiter_deferred_data, 1 * MSEC);
	}
}

int usart4_iface_request(usb_uint *rx_buf, usb_uint *tx_buf)
{
	struct usb_setup_packet req;

	if (!rx_buf)
		return -1;

	usb_read_setup_packet(rx_buf, &req);

	if (req.wIndex != USB_IFACE_USART4_STREAM)
		return -1;

	if (req.bmRequestType ==
	    (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE)) {
		uint16_t response;

		if (req.wLength < sizeof(response))
			return -1;

		switch (req.bRequest) {
		case USB_USART_REQ_PARITY:
			response = 0; /* Strict 8N1 (no parity) */
			break;
		case USB_USART_REQ_BAUD:
			/* Locked 1.000 Mbps in 100-baud units (10000 = 0x2710)
			 */
			response = DIV_ROUND_NEAREST(1000000 + 50, 100);
			break;
		default:
			return -1;
		}

		memcpy_to_usbram((void *)usb_sram_addr(tx_buf), &response,
				 sizeof(response));
		btable_ep[0].tx_count = sizeof(response);
		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, 0);
		return 0;
	}

	if (req.bmRequestType !=
	    (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE))
		return -1;

	if (req.wLength != 0)
		return -1;

	switch (req.bRequest) {
	case USB_USART_SET_PARITY:
	case USB_USART_SET_BAUD:
	case USB_USART_BREAK:
		/*
		 * Absorb host PTY parity, baud-rate, and break requests as
		 * no-ops so hardware USART4 stays locked at 1.000 Mbps 8N1.
		 */
		break;
	case USB_USART_CLEAR_QUEUES: {
		enum clear_which_fifo which =
			(req.wValue == 0) ? CLEAR_BOTH_FIFOS :
					    (enum clear_which_fifo)req.wValue;

		if (which & CLEAR_RX_FIFO)
			usb_stream_clear_tx(&usart4_usb);
		if (which & CLEAR_TX_FIFO) {
			usb_stream_clear_rx(&usart4_usb);
			kbd_in_flight = false;
			bridge.pending_key_cmd = false;
		}
		break;
	}
	default:
		return -1;
	}

	btable_ep[0].tx_count = 0;
	STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, EP_STATUS_OUT);
	return 0;
}
USB_DECLARE_IFACE(USB_IFACE_USART4_STREAM, usart4_iface_request);

int bridge_iface_request(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx)
{
	if (!ep0_buf_rx)
		return -1;

	if (ep0_buf_rx[0] ==
	    (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE |
	     (USB_REQ_SET_INTERFACE << 8))) {
		if (ep0_buf_rx[1] != 0)
			return -1;

		btable_ep[USB_EP_BRIDGE].tx_count = 0;
		btable_ep[USB_EP_BRIDGE].rx_count = usb_ep_rx_size(64);
		ep7_out_pending = 0;
		ep7_zlp_in_flight = false;
		STM32_TOGGLE_EP(USB_EP_BRIDGE,
				EP_TX_RX_MASK | EP_RX_DTOG | EP_TX_DTOG,
				(effective_usb_protocol != SERVO_PROTO_NONE &&
				 !dppu_pulse_active) ?
					(EP_TX_NAK | EP_RX_VALID) :
					(EP_TX_STALL | EP_RX_STALL),
				EP_CTR_BOTH);
		if (bridge.usb_in_busy || !bridge.tx_credits ||
		    bridge.has_tx_data) {
			host_usb_reset_irq = true;
			hook_call_deferred(&bridge_arbiter_deferred_data, 0);
		}
		btable_ep[0].tx_count = 0;
		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_RX_VALID | EP_TX_VALID, 0);
		return 0;
	}

	if (ep0_buf_rx[0] ==
	    (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE |
	     (USB_REQ_GET_INTERFACE << 8))) {
		ep0_buf_tx[0] = 0;
		btable_ep[0].tx_count = 1;
		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_RX_VALID | EP_TX_VALID,
				EP_STATUS_OUT);
		return 0;
	}

	return -1;
}
USB_DECLARE_IFACE(USB_IFACE_BRIDGE, bridge_iface_request);

int usb_bridge_clear_halt(uint8_t ep_addr)
{
	if (effective_usb_protocol == SERVO_PROTO_NONE || dppu_pulse_active)
		return -1;

	if (ep_addr == (USB_EP_BRIDGE | 0x80)) {
		btable_ep[USB_EP_BRIDGE].tx_count = 0;
		ep7_zlp_in_flight = false;
		STM32_TOGGLE_EP(USB_EP_BRIDGE, EP_TX_MASK | EP_TX_DTOG,
				EP_TX_NAK, EP_CTR_BOTH);
		if (bridge.usb_in_busy || !bridge.tx_credits ||
		    bridge.has_tx_data) {
			host_usb_reset_irq = true;
			hook_call_deferred(&bridge_arbiter_deferred_data, 0);
		}
	} else if (ep_addr == USB_EP_BRIDGE) {
		ep7_out_pending = 0;
		btable_ep[USB_EP_BRIDGE].rx_count = usb_ep_rx_size(64);
		STM32_TOGGLE_EP(USB_EP_BRIDGE, EP_RX_MASK | EP_RX_DTOG,
				EP_RX_VALID, EP_CTR_BOTH);
		if (bridge.usb_in_busy || !bridge.tx_credits ||
		    bridge.has_tx_data) {
			host_usb_reset_irq = true;
			hook_call_deferred(&bridge_arbiter_deferred_data, 0);
		}
	} else {
		return -1;
	}

	btable_ep[0].tx_count = 0;
	STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_RX_VALID | EP_TX_VALID, 0);
	return 0;
}
