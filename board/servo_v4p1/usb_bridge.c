/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "hooks.h"
#include "link_defs.h"
#include "queue.h"
#include "registers.h"
#define SERVO_BRIDGE_FIXED_LEADER 1
#include "servo_usb_bridge_frame.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usart.h"
#include "usb-stream.h"
#include "usb_api.h"
#include "usb_bridge.h"
#include "usb_hw.h"
#include "usb_pd.h"
#include "util.h"

#ifndef DEFAULT_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"
#endif

static inline int servo_strcmp(const char *s1, const char *s2)
{
	while (*s1 && (*s1 == *s2)) {
		s1++;
		s2++;
	}
	return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}
#ifndef strcmp
#define strcmp(s1, s2) servo_strcmp((s1), (s2))
#endif

#ifndef STM32_USART_CR2_STOP_2B
#define STM32_USART_CR2_STOP_2B BIT(13)
#endif

static struct servo_bridge_node bridge;
static uint8_t active_usb_protocol = SERVO_PROTO_NONE;
static uint8_t effective_usb_protocol = SERVO_PROTO_NONE;
static uint8_t prev_effective_proto = SERVO_PROTO_NONE;
static uint8_t last_enumerated_proto = SERVO_PROTO_NONE;
static bool dppu_pulse_active;
static volatile bool ep7_in_drained_irq;
static volatile uint8_t ep7_out_pending;
static bool ep7_out_session_handled;
static volatile bool host_usb_reset_irq;
static uint32_t last_rx_corrupt;
static uint32_t last_rx_fe;
static size_t ep_bridge_rx_len;
static bool ep7_zlp_in_flight;
static uint8_t fb_tx_64b_count;
static uint32_t last_ep7_in_drain_us;
static bool kbd_in_flight;
static uint32_t last_key_cmd_us;
static volatile uint8_t ep2_out_pending;
static uint8_t latest_msc_req_tag;
static bool pending_serial_sync;

#define SERVO_PROBE_WAIT_BOOT_1M_US (250 * MSEC)
#define SERVO_PROBE_9600_TIMEOUT_US 10000U
#define SERVO_PROBE_9600_MIN_TURNAROUND_US 1900U
#define SERVO_PROBE_9600_MAX_TURNAROUND_US 6000U
#define SERVO_PROBE_9600_CONFIRM_COUNT 2U
#define SERVO_PROBE_ACTIVE_1M_TIMEOUT_US (50 * MSEC)

static enum usb_bridge_probe_state probe_state = USB_BRIDGE_PROBE_WAIT_BOOT_1M;
static uint32_t probe_state_enter_us;
static uint32_t probe_tx_start_us;
static uint8_t probe_9600_hits;
static bool atmel_reset_held;
static uint32_t usart4_virtual_baud = 1000000U;

static usb_uint ep_bridge_rx_buffer[64 / 2] __usb_ram;
static usb_uint ep_bridge_tx_buffer[64 / 2] __usb_ram;
static usb_uint ep_msc_rx_buffer[64 / 2] __usb_ram;
static usb_uint ep_msc_tx_buffer[64 / 2] __usb_ram;

const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_MSC) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_MSC,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = 0x50,
	.bInterfaceProtocol = 0x01,
	.iInterface = 0,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_MSC, 0) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | USB_EP_MSC,
	.bmAttributes = 0x02 /* Bulk IN */,
	.wMaxPacketSize = 64,
	.bInterval = 0,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_MSC, 1) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_EP_MSC,
	.bmAttributes = 0x02 /* Bulk OUT */,
	.wMaxPacketSize = 64,
	.bInterval = 0,
};

static void usb_msc_tx(void);
static void usb_msc_rx(void);
static void usb_msc_event(enum usb_ep_event evt);

USB_DECLARE_EP(USB_EP_MSC, usb_msc_tx, usb_msc_rx, usb_msc_event);

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

static void ep_toggle_safe(int ep, uint16_t mask, uint16_t val)
{
	interrupt_disable();
	STM32_TOGGLE_EP(ep, mask, val, EP_CTR_BOTH);
	interrupt_enable();
}

static inline void ep7_toggle_safe(uint16_t mask, uint16_t val)
{
	ep_toggle_safe(USB_EP_BRIDGE, mask, val);
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
	effective_usb_protocol =
		(pd_is_connected(1) &&
		 probe_state != USB_BRIDGE_LOCKED_9600_LEGACY) ?
			active_usb_protocol :
			SERVO_PROTO_NONE;

	/* 1. Transition State Cleanup */
	if (effective_usb_protocol != prev_effective_proto) {
		ep7_out_pending = 0;
		ep7_out_session_handled = false;
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
			if (probe_state == USB_BRIDGE_LOCKED_1M_BRIDGE) {
				if (!bridge.usb_in_busy && !ep7_zlp_in_flight) {
					btable_ep[USB_EP_BRIDGE].tx_count = 0;
					ep7_toggle_safe(EP_TX_MASK, EP_TX_NAK);
				}
				if (!ep7_out_pending && !bridge.has_tx_data) {
					btable_ep[USB_EP_BRIDGE].rx_count =
						usb_ep_rx_size(64);
					ep7_toggle_safe(EP_RX_MASK,
							EP_RX_VALID);
				}
			} else {
				ep7_zlp_in_flight = false;
				btable_ep[USB_EP_BRIDGE].tx_count = 0;
				ep7_toggle_safe(EP_TX_RX_MASK,
						EP_TX_NAK | EP_RX_NAK);
			}
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
	btable_ep[USB_EP_MSC].tx_count = 0;
	btable_ep[USB_EP_MSC].rx_count = usb_ep_rx_size(64);
	ep_toggle_safe(USB_EP_MSC, EP_TX_RX_MASK | EP_RX_DTOG | EP_TX_DTOG,
		       EP_TX_NAK | EP_RX_VALID);
}

static void usart4_configure_line(int baud, bool two_stop_bits)
{
	intptr_t base = usart4.hw->base;

	interrupt_disable();
	task_disable_irq(STM32_IRQ_USART3_4);
	STM32_USART_CR1(base) &= ~STM32_USART_CR1_UE;

	queue_init(&usart4_to_bridge);
	queue_init(&bridge_to_usart4);

	usart4.tx->init(&usart4);
	usart4.rx->init(&usart4);

	if (two_stop_bits)
		STM32_USART_CR2(base) |= STM32_USART_CR2_STOP_2B;
	else
		STM32_USART_CR2(base) &= ~STM32_USART_CR2_STOP_2B;

	usart_set_baud(&usart4, baud);
	STM32_USART_CR1(base) |= STM32_USART_CR1_UE;
	task_enable_irq(STM32_IRQ_USART3_4);
	interrupt_enable();

	servo_frame_rx_init(&bridge.rx);
	last_rx_corrupt = (uint32_t)usart4.state->rx_overrun +
			  (uint32_t)usart4.state->rx_dropped;
	last_rx_fe = (uint32_t)usart4.state->rx_framing_error;

	/*
	 * Wait 20 us for the STM32 USART initial 11-bit idle frame (RM0091
	 * 27.5.2) to complete and allow the peer UART receiver to recover
	 * after any prior baud rate switch before DMA TX starts.
	 */
	udelay(20);
}

static void bridge_reinit_preserve_proto(uint32_t now_us)
{
	uint8_t saved_proto = (bridge.desired_proto != SERVO_PROTO_NONE) ?
				      bridge.desired_proto :
				      bridge.active_proto;

	servo_bridge_init(&bridge, /*is_epoch_leader=*/true, now_us);
	bridge.active_proto = saved_proto;
	bridge.desired_proto = saved_proto;
}

void usb_bridge_notify_atmel_reset(int released)
{
	uint8_t discard;

	if (released && probe_state != USB_BRIDGE_PROBE_RESET_HELD)
		return;
	if (!released && probe_state == USB_BRIDGE_PROBE_RESET_HELD)
		return;

	uint32_t now_us = (uint32_t)get_time().val;

	kbd_in_flight = false;
	bridge.pending_key_cmd = false;
	host_usb_reset_irq = false;
	while (queue_remove_unit(&usb_to_bridge_kbd, &discard))
		;

	if (!released) {
		atmel_reset_held = true;
		probe_state = USB_BRIDGE_PROBE_RESET_HELD;
		bridge_reinit_preserve_proto(now_us);
		usb_bridge_update_effective_proto();
	} else {
		atmel_reset_held = false;
		probe_state = USB_BRIDGE_PROBE_WAIT_BOOT_1M;
		probe_state_enter_us = now_us;
		probe_9600_hits = 0;
		bridge_reinit_preserve_proto(now_us);
		usb_bridge_update_effective_proto();
		usart4_configure_line(1000000, /*two_stop_bits=*/true);
		servo_frame_rx_init(&bridge.rx);
	}
	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

enum usb_bridge_probe_state usb_bridge_get_probe_state(void)
{
	return probe_state;
}

void usb_bridge_init(void)
{
	ep7_out_pending = 0;
	ep7_out_session_handled = false;
	ep_bridge_rx_len = 0;
	ep7_zlp_in_flight = false;
	fb_tx_64b_count = 0;
	kbd_in_flight = false;
	host_usb_reset_irq = false;
	ep2_out_pending = 0;
	latest_msc_req_tag = 0;
	pending_serial_sync = true;
	atmel_reset_held = false;
	active_usb_protocol = SERVO_PROTO_NONE;
	effective_usb_protocol = SERVO_PROTO_NONE;
	prev_effective_proto = SERVO_PROTO_NONE;
	probe_state = USB_BRIDGE_PROBE_WAIT_BOOT_1M;
	probe_state_enter_us = (uint32_t)get_time().val;
	probe_9600_hits = 0;

	set_descriptor_patch(USB_DESC_BRIDGE,
			     &usb_desc_iface6_0iface.bInterfaceSubClass,
			     0x0000);

	servo_bridge_init(&bridge, /*is_epoch_leader=*/true,
			  (uint32_t)get_time().val);

	usart4_configure_line(1000000, /*two_stop_bits=*/true);

	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static void usb_msc_tx(void)
{
	STM32_TOGGLE_EP(USB_EP_MSC, 0, 0, EP_CTR_RX);
	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static void usb_msc_rx(void)
{
	STM32_TOGGLE_EP(USB_EP_MSC, 0, 0, EP_CTR_TX);
	ep2_out_pending = 1;
	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static void usb_msc_event(enum usb_ep_event evt)
{
	if (evt == USB_EVENT_RESET) {
		ep2_out_pending = 0;
		btable_ep[USB_EP_MSC].tx_addr = usb_sram_addr(ep_msc_tx_buffer);
		btable_ep[USB_EP_MSC].tx_count = 0;
		btable_ep[USB_EP_MSC].rx_addr = usb_sram_addr(ep_msc_rx_buffer);
		btable_ep[USB_EP_MSC].rx_count = usb_ep_rx_size(64);

		STM32_USB_EP(USB_EP_MSC) = USB_EP_MSC | EP_TX_NAK | EP_RX_VALID;
	}
}

static void usb_bridge_tx(void)
{
	STM32_TOGGLE_EP(USB_EP_BRIDGE, 0, 0, EP_CTR_RX);
	ep7_in_drained_irq = true;
	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static bool ep7_rx_buf_is_adb_cnxn(void)
{
	return effective_usb_protocol == SERVO_PROTO_ADB &&
	       (btable_ep[USB_EP_BRIDGE].rx_count & RX_COUNT_MASK) == 24 &&
	       ep_bridge_rx_buffer[0] == 0x4E43 &&
	       ep_bridge_rx_buffer[1] == 0x4E58 &&
	       ep_bridge_rx_buffer[10] == 0xB1BC &&
	       ep_bridge_rx_buffer[11] == 0xB1A7;
}

static void usb_bridge_rx(void)
{
	if (ep7_rx_buf_is_adb_cnxn() &&
	    (bridge.adb_cnxn_resp_rem_chunks == 0U ||
	     (ep7_in_drained_irq && !ep7_zlp_in_flight &&
	      bridge.adb_cnxn_resp_rem_chunks == 1U))) {
		ep7_zlp_in_flight = false;
		btable_ep[USB_EP_BRIDGE].tx_count = 0;
		STM32_TOGGLE_EP(USB_EP_BRIDGE, EP_TX_MASK, EP_TX_NAK,
				EP_CTR_TX);
	} else {
		STM32_TOGGLE_EP(USB_EP_BRIDGE, 0, 0, EP_CTR_TX);
	}
	ep7_out_pending = 1;
	ep7_out_session_handled = false;
	hook_call_deferred(&bridge_arbiter_deferred_data, 0);
}

static void usb_bridge_event(enum usb_ep_event evt)
{
	if (evt == USB_EVENT_RESET) {
		if (!dppu_pulse_active) {
			last_enumerated_proto = effective_usb_protocol;
			hook_call_deferred(&bridge_dppu_debounce_deferred_data,
					   -1);
		}

		btable_ep[USB_EP_BRIDGE].tx_addr =
			usb_sram_addr(ep_bridge_tx_buffer);
		btable_ep[USB_EP_BRIDGE].tx_count = 0;
		btable_ep[USB_EP_BRIDGE].rx_addr =
			usb_sram_addr(ep_bridge_rx_buffer);
		ep7_out_pending = 0;
		ep7_out_session_handled = false;
		btable_ep[USB_EP_BRIDGE].rx_count = usb_ep_rx_size(64);

		STM32_USB_EP(USB_EP_BRIDGE) =
			USB_EP_BRIDGE |
			((effective_usb_protocol != SERVO_PROTO_NONE &&
			  effective_usb_protocol == last_enumerated_proto &&
			  !dppu_pulse_active) ?
				 ((probe_state == USB_BRIDGE_LOCKED_1M_BRIDGE) ?
					  (EP_TX_NAK | EP_RX_VALID) :
					  (EP_TX_NAK | EP_RX_NAK)) :
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

static void bridge_process_uart_rx(uint32_t now_us)
{
	uint8_t ch;

	while (queue_remove_unit(&usart4_to_bridge, &ch)) {
		struct servo_bridge_rx_effects fx;
		enum servo_rx_event evt =
			servo_frame_rx_step(&bridge.rx, ch, now_us);
		bool lock_transition = false;

		if (probe_state == USB_BRIDGE_PROBE_WAIT_BOOT_1M ||
		    probe_state == USB_BRIDGE_PROBE_ACTIVE_1M) {
			if (evt != SERVO_RX_EVT_FRAME_COMPLETE)
				continue;
			probe_state = USB_BRIDGE_LOCKED_1M_BRIDGE;
			lock_transition = true;
		} else if (evt == SERVO_RX_EVT_NONE &&
			   !bridge.rx.timeout_occurred) {
			continue;
		}

		servo_bridge_on_rx_event(&bridge, evt, &fx);

		if (fx.proto_changed || lock_transition) {
			active_usb_protocol = bridge.active_proto;
			usb_bridge_update_effective_proto();
		}

		if (fx.epoch_history_cleared) {
			ep7_zlp_in_flight = false;
			ep7_out_session_handled = false;
			fb_tx_64b_count = 0;
			if (effective_usb_protocol != SERVO_PROTO_NONE &&
			    effective_usb_protocol == last_enumerated_proto &&
			    !dppu_pulse_active) {
				btable_ep[USB_EP_BRIDGE].tx_count = 0;
				if (!ep7_out_pending) {
					btable_ep[USB_EP_BRIDGE].rx_count =
						usb_ep_rx_size(64);
					ep7_toggle_safe(EP_TX_RX_MASK,
							EP_TX_NAK |
								EP_RX_VALID);
				} else {
					ep7_toggle_safe(EP_TX_MASK, EP_TX_NAK);
				}
			}
		}

		if (fx.usb_in_armed &&
		    servo_adb_session_filter_rx_data(&bridge, &fx)) {
			if (effective_usb_protocol != SERVO_PROTO_NONE &&
			    effective_usb_protocol == last_enumerated_proto &&
			    !dppu_pulse_active) {
				ep7_zlp_in_flight = false;
				memcpy_to_usbram((void *)usb_sram_addr(
							 ep_bridge_tx_buffer),
						 fx.usb_in_payload,
						 fx.usb_in_len);
				btable_ep[USB_EP_BRIDGE].tx_count =
					fx.usb_in_len;
				ep7_toggle_safe(EP_TX_MASK, EP_TX_VALID);
				if (bridge.active_proto ==
				    SERVO_PROTO_FASTBOOT) {
					fb_tx_64b_count =
						(fx.usb_in_len == 64) ?
							((fb_tx_64b_count + 1) &
							 0x03) :
							0;
				}
			} else {
				servo_bridge_drain_usb_in(&bridge);
			}
		}

		if (fx.key_ack_received) {
			kbd_in_flight = false;
			queue_add_unit(&bridge_kbd_to_usb, &fx.key_ack_ch);
		}

		if (fx.msc_frame_received &&
		    fx.msc_frame_type == SERVO_FRAME_MSC_REQ &&
		    fx.msc_frame_len == sizeof(struct servo_msc_req)) {
			latest_msc_req_tag = fx.msc_frame_payload[1];
			if (bridge.pending_msc_len > 0 &&
			    bridge.pending_msc_type == SERVO_FRAME_MSC_DATA &&
			    bridge.pending_msc_buf[0] != SERVO_MSC_RSP_SERIAL &&
			    bridge.pending_msc_buf[1] != latest_msc_req_tag) {
				bridge.pending_msc_len = 0;
			}
			interrupt_disable();
			STM32_TOGGLE_EP(USB_EP_MSC, EP_TX_MASK, EP_TX_NAK,
					EP_CTR_BOTH);
			memcpy_to_usbram(
				(void *)usb_sram_addr(ep_msc_tx_buffer),
				fx.msc_frame_payload,
				sizeof(struct servo_msc_req));
			btable_ep[USB_EP_MSC].tx_count =
				sizeof(struct servo_msc_req);
			STM32_TOGGLE_EP(USB_EP_MSC, EP_TX_MASK, EP_TX_VALID,
					EP_CTR_BOTH);
			interrupt_enable();
		}

		if (evt == SERVO_RX_EVT_FRAME_COMPLETE &&
		    ((bridge.rx.type & SERVO_OPCODE_MASK) ==
			     SERVO_FRAME_RESET_REQ ||
		     (bridge.rx.type & SERVO_OPCODE_MASK) ==
			     SERVO_FRAME_RESET_ACK) &&
		    (bridge.rx.arg_len & SERVO_RESET_FLAG_BOOT) != 0) {
			kbd_in_flight = false;
			pending_serial_sync = true;
		}
	}
}

static void bridge_handle_ep7_in_drained(uint32_t now_us)
{
	if (ep7_in_drained_irq) {
		ep7_in_drained_irq = false;
		last_ep7_in_drain_us = now_us;
		if (ep7_zlp_in_flight) {
			ep7_zlp_in_flight = false;
		} else if (bridge.usb_in_busy) {
			servo_adb_session_host_drain_usb_in(&bridge);
		}
	}
}

static void bridge_handle_ep7_out_session(uint32_t now_us)
{
	uint8_t pkt[SERVO_MAX_DATA_LEN] = { 0 };
	bool disarm = false;

	bridge_handle_ep7_in_drained(now_us);

	if (probe_state != USB_BRIDGE_LOCKED_1M_BRIDGE ||
	    bridge.state != SERVO_STATE_ACTIVE || !ep7_out_pending ||
	    ep7_out_session_handled)
		return;

	ep_bridge_rx_len = btable_ep[USB_EP_BRIDGE].rx_count & RX_COUNT_MASK;
	if (ep_bridge_rx_len > SERVO_MAX_DATA_LEN)
		ep_bridge_rx_len = SERVO_MAX_DATA_LEN;
	if (ep_bridge_rx_len > 0) {
		memcpy_from_usbram(pkt,
				   (void *)usb_sram_addr(ep_bridge_rx_buffer),
				   ep_bridge_rx_len);
	}
	if (!servo_adb_session_on_host_out(
		    &bridge, pkt, (uint8_t)ep_bridge_rx_len, &disarm)) {
		ep7_out_pending = 0;
		ep7_out_session_handled = false;
		btable_ep[USB_EP_BRIDGE].rx_count = usb_ep_rx_size(64);
		ep7_toggle_safe(EP_RX_MASK, EP_RX_VALID);
	} else {
		if (disarm)
			ep7_disarm_tx_preserve_dtog();
		ep7_out_session_handled = true;
	}
}

static void bridge_arbiter_deferred(void)
{
	uint32_t now_us = (uint32_t)get_time().val;
	uint8_t ch;
	uint8_t frame_buf[SERVO_MAX_FRAME_BYTES];
	uint8_t frame_len;
	uint32_t cur_corrupt = (uint32_t)usart4.state->rx_overrun +
			       (uint32_t)usart4.state->rx_dropped;
	uint32_t cur_fe = (uint32_t)usart4.state->rx_framing_error;
	bool rx_corrupted = (cur_corrupt != last_rx_corrupt);
	bool rx_fe = (cur_fe != last_rx_fe);

	if (probe_state == USB_BRIDGE_PROBE_RESET_HELD) {
		while (queue_remove_unit(&usb_to_bridge_kbd, &ch))
			;
		return;
	}

	if (bridge.state == SERVO_STATE_ACTIVE &&
	    probe_state != USB_BRIDGE_LOCKED_1M_BRIDGE) {
		probe_state = USB_BRIDGE_LOCKED_1M_BRIDGE;
		usb_bridge_update_effective_proto();
	}

	if (probe_state == USB_BRIDGE_LOCKED_9600_LEGACY) {
		if (rx_fe) {
			last_rx_fe = cur_fe;
			last_rx_corrupt = cur_corrupt;
			host_usb_reset_irq = false;
			probe_state = USB_BRIDGE_PROBE_WAIT_BOOT_1M;
			probe_state_enter_us = now_us;
			probe_9600_hits = 0;
			bridge_reinit_preserve_proto(now_us);
			usb_bridge_update_effective_proto();
			usart4_configure_line(1000000, /*two_stop_bits=*/true);
			servo_frame_rx_init(&bridge.rx);
			hook_call_deferred(&bridge_arbiter_deferred_data, 0);
			return;
		}

		while (queue_remove_unit(&usb_to_bridge_kbd, &ch))
			queue_add_unit(&bridge_to_usart4, &ch);

		while (queue_remove_unit(&usart4_to_bridge, &ch))
			queue_add_unit(&bridge_kbd_to_usb, &ch);

		if (!queue_is_empty(&usb_to_bridge_kbd) ||
		    !queue_is_empty(&usart4_to_bridge))
			hook_call_deferred(&bridge_arbiter_deferred_data, 0);
		return;
	}

	if (probe_state == USB_BRIDGE_PROBE_WAIT_BOOT_1M) {
		host_usb_reset_irq = false;
		if (rx_corrupted) {
			last_rx_corrupt = cur_corrupt;
			rx_corrupted = false;
			interrupt_disable();
			queue_init(&usart4_to_bridge);
			interrupt_enable();
			servo_frame_rx_init(&bridge.rx);
			servo_bridge_on_corrupt_rx(&bridge);
		}

		bridge_process_uart_rx(now_us);

		if (probe_state == USB_BRIDGE_PROBE_WAIT_BOOT_1M) {
			if ((uint32_t)(now_us - probe_state_enter_us) >=
			    SERVO_PROBE_WAIT_BOOT_1M_US) {
				probe_state = USB_BRIDGE_PROBE_9600;
				probe_state_enter_us = now_us;
				probe_9600_hits = 0;
				usart4_configure_line(9600,
						      /*two_stop_bits=*/false);
				ch = 0x00;
				queue_add_unit(&bridge_to_usart4, &ch);
				probe_tx_start_us = now_us;
			}
			hook_call_deferred(&bridge_arbiter_deferred_data,
					   1 * MSEC);
			return;
		}
	} else if (probe_state == USB_BRIDGE_PROBE_9600) {
		bool transition_to_active_1m = false;

		if (rx_fe) {
			last_rx_fe = cur_fe;
			transition_to_active_1m = true;
		}

		while (queue_remove_unit(&usart4_to_bridge, &ch)) {
			uint32_t dt = (uint32_t)(now_us - probe_tx_start_us);
			if (dt < SERVO_PROBE_9600_MIN_TURNAROUND_US) {
				transition_to_active_1m = true;
			} else if (ch == 0xFF && !rx_corrupted &&
				   dt <= SERVO_PROBE_9600_MAX_TURNAROUND_US) {
				probe_9600_hits++;
				if (probe_9600_hits >=
				    SERVO_PROBE_9600_CONFIRM_COUNT) {
					probe_state =
						USB_BRIDGE_LOCKED_9600_LEGACY;
					bridge.active_proto = SERVO_PROTO_NONE;
					bridge.desired_proto = SERVO_PROTO_NONE;
					active_usb_protocol = SERVO_PROTO_NONE;
					usb_bridge_update_effective_proto();
					hook_call_deferred(
						&bridge_arbiter_deferred_data,
						0);
					return;
				}
				ch = 0x00;
				queue_add_unit(&bridge_to_usart4, &ch);
				probe_tx_start_us = now_us;
				probe_state_enter_us = now_us;
			} else {
				transition_to_active_1m = true;
			}
		}

		if (!transition_to_active_1m &&
		    (uint32_t)(now_us - probe_state_enter_us) >=
			    SERVO_PROBE_9600_TIMEOUT_US) {
			transition_to_active_1m = true;
		}

		if (transition_to_active_1m) {
			host_usb_reset_irq = false;
			probe_state = USB_BRIDGE_PROBE_ACTIVE_1M;
			probe_state_enter_us = now_us;
			usart4_configure_line(1000000, /*two_stop_bits=*/true);
			servo_frame_rx_init(&bridge.rx);
			bridge_reinit_preserve_proto(now_us);
		} else {
			hook_call_deferred(&bridge_arbiter_deferred_data,
					   1 * MSEC);
			return;
		}
	} else if (probe_state == USB_BRIDGE_PROBE_ACTIVE_1M) {
		host_usb_reset_irq = false;
		if (rx_corrupted) {
			last_rx_corrupt = cur_corrupt;
			rx_corrupted = false;
			interrupt_disable();
			queue_init(&usart4_to_bridge);
			interrupt_enable();
			servo_frame_rx_init(&bridge.rx);
			servo_bridge_on_corrupt_rx(&bridge);
		}

		bridge_process_uart_rx(now_us);

		if (probe_state == USB_BRIDGE_PROBE_ACTIVE_1M) {
			if ((uint32_t)(now_us - probe_state_enter_us) >=
			    SERVO_PROBE_ACTIVE_1M_TIMEOUT_US) {
				host_usb_reset_irq = false;
				probe_state = USB_BRIDGE_PROBE_WAIT_BOOT_1M;
				probe_state_enter_us = now_us;
				servo_bridge_init(&bridge,
						  /*is_epoch_leader=*/true,
						  now_us);
				bridge.pending_reset_req = false;
				active_usb_protocol = SERVO_PROTO_NONE;
				usb_bridge_update_effective_proto();
				servo_frame_rx_init(&bridge.rx);
				hook_call_deferred(
					&bridge_arbiter_deferred_data,
					1 * MSEC);
				return;
			}
		}
	}

	for (int pass = 0; pass < 4; pass++) {
		/* 1. Hardware Microsecond Timebase & Deferred IRQ Events */
		if (host_usb_reset_irq) {
			host_usb_reset_irq = false;
			ep7_zlp_in_flight = false;
			ep7_out_session_handled = false;
			fb_tx_64b_count = 0;
			servo_bridge_trigger_reset(&bridge, now_us);
		}

		bridge_handle_ep7_in_drained(now_us);

		if (rx_corrupted) {
			last_rx_corrupt = cur_corrupt;
			rx_corrupted = false;
			interrupt_disable();
			queue_init(&usart4_to_bridge);
			interrupt_enable();
			servo_frame_rx_init(&bridge.rx);
			servo_bridge_on_corrupt_rx(&bridge);
		}

		bridge_handle_ep7_out_session(now_us);

		/* 2. UART RX Parsing & State Machine Dispatch */
		bridge_process_uart_rx(now_us);

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

		/* 4. Inbound Keyboard, Bulk OUT, & MSC Staging */
		if (probe_state == USB_BRIDGE_LOCKED_1M_BRIDGE) {
			if (servo_bridge_can_send_key_cmd(&bridge,
							  kbd_in_flight) &&
			    queue_count(&usb_to_bridge_kbd) > 0) {
				uint8_t kch;
				if (queue_remove_unit(&usb_to_bridge_kbd,
						      &kch)) {
					servo_bridge_send_key_cmd(&bridge, kch);
					kbd_in_flight = true;
					last_key_cmd_us = now_us;
				}
			}

			if (pending_serial_sync &&
			    bridge.state == SERVO_STATE_ACTIVE &&
			    bridge.pending_msc_len == 0U) {
				pending_serial_sync = false;
				const char *serial = board_read_serial();
				if (serial && serial[0] != '\0' &&
				    strcmp(serial, DEFAULT_SERIALNO) != 0) {
					uint8_t slen = 0;
					while (slen < 32U &&
					       serial[slen] != '\0')
						slen++;
					uint8_t spkt[36];
					spkt[0] = SERVO_MSC_RSP_SERIAL;
					spkt[1] = 0;
					spkt[2] = slen;
					spkt[3] = 0;
					for (uint8_t i = 0; i < slen; i++)
						spkt[4 + i] =
							(uint8_t)serial[i];
					servo_bridge_send_msc_data(
						&bridge, spkt,
						(uint8_t)(4U + slen));
				}
			}

			if (ep2_out_pending) {
				uint8_t pkt[SERVO_MAX_DATA_LEN];
				size_t rx_len = btable_ep[USB_EP_MSC].rx_count &
						RX_COUNT_MASK;
				if (rx_len > SERVO_MAX_DATA_LEN)
					rx_len = SERVO_MAX_DATA_LEN;
				if (rx_len > 0) {
					memcpy_from_usbram(
						pkt,
						(void *)usb_sram_addr(
							ep_msc_rx_buffer),
						rx_len);
				}
				if (rx_len < 2U ||
				    pkt[1] != latest_msc_req_tag) {
					/*
					 * Stale req_tag fast-drain at 12 Mbps
					 * USB speed
					 * (NoStaleTagForwardedToUsart4).
					 */
					ep2_out_pending = 0;
					btable_ep[USB_EP_MSC].rx_count =
						usb_ep_rx_size(64);
					ep_toggle_safe(USB_EP_MSC, EP_RX_MASK,
						       EP_RX_VALID);
				} else if (bridge.pending_msc_len == 0U) {
					servo_bridge_send_msc_data(
						&bridge, pkt, (uint8_t)rx_len);
					ep2_out_pending = 0;
					btable_ep[USB_EP_MSC].rx_count =
						usb_ep_rx_size(64);
					ep_toggle_safe(USB_EP_MSC, EP_RX_MASK,
						       EP_RX_VALID);
				}
			}

			if (ep7_out_pending) {
				bridge_handle_ep7_out_session(now_us);
				if (ep7_out_pending &&
				    servo_bridge_can_send_data(&bridge)) {
					uint8_t pkt[SERVO_MAX_DATA_LEN];

					ep_bridge_rx_len =
						btable_ep[USB_EP_BRIDGE]
							.rx_count &
						RX_COUNT_MASK;
					if (ep_bridge_rx_len >
					    SERVO_MAX_DATA_LEN)
						ep_bridge_rx_len =
							SERVO_MAX_DATA_LEN;
					if (ep_bridge_rx_len > 0) {
						memcpy_from_usbram(
							pkt,
							(void *)usb_sram_addr(
								ep_bridge_rx_buffer),
							ep_bridge_rx_len);
					}
					servo_bridge_send_data(
						&bridge, pkt, ep_bridge_rx_len);
					ep7_out_pending = 0;
					ep7_out_session_handled = false;
					btable_ep[USB_EP_BRIDGE].rx_count =
						usb_ep_rx_size(64);
					ep7_toggle_safe(EP_RX_MASK,
							EP_RX_VALID);
				}
			}
		}

		/* 5. Packet Arbiter TX Scheduling */
		while ((frame_len = servo_bridge_pop_tx_frame(
				&bridge, queue_space(&bridge_to_usart4),
				frame_buf, now_us)) > 0) {
			queue_add_units(&bridge_to_usart4, frame_buf,
					frame_len);
		}

		cur_corrupt = (uint32_t)usart4.state->rx_overrun +
			      (uint32_t)usart4.state->rx_dropped;
		rx_corrupted = (cur_corrupt != last_rx_corrupt);

		if (queue_is_empty(&usart4_to_bridge) && !ep7_in_drained_irq &&
		    !host_usb_reset_irq && !rx_corrupted)
			break;

		now_us = (uint32_t)get_time().val;
	}

	/* 6. High-Precision Re-Arming */
	if (probe_state != USB_BRIDGE_LOCKED_1M_BRIDGE) {
		hook_call_deferred(&bridge_arbiter_deferred_data, 1 * MSEC);
		return;
	}

	cur_corrupt = (uint32_t)usart4.state->rx_overrun +
		      (uint32_t)usart4.state->rx_dropped;
	rx_corrupted = (cur_corrupt != last_rx_corrupt);

	if (!queue_is_empty(&usart4_to_bridge) || ep7_in_drained_irq ||
	    host_usb_reset_irq || rx_corrupted) {
		hook_call_deferred(&bridge_arbiter_deferred_data, 0);
	} else if (!queue_is_empty(&usb_to_bridge_kbd) ||
		   bridge.rx.state != SERVO_RX_WAIT_SYNC ||
		   bridge.tx_credits == 0 ||
		   bridge.state == SERVO_STATE_RESETTING || kbd_in_flight ||
		   bridge.pending_reset_req || bridge.pending_reset_ack ||
		   bridge.pending_credit || bridge.pending_nak ||
		   bridge.pending_key_cmd || bridge.pending_data_tx ||
		   bridge.pending_msc_len > 0 || ep2_out_pending ||
		   pending_serial_sync || ep7_out_pending ||
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
			/*
			 * Report 1,000,000 baud (10000 in 100-baud units) only
			 * when auto-baud has locked onto the 1M composite
			 * bridge firmware; otherwise report 9600 baud (96).
			 */
			response =
				(probe_state == USB_BRIDGE_LOCKED_1M_BRIDGE) ?
					(1000000 / 100) :
					(9600 / 100);
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
		break;
	case USB_USART_SET_BAUD: {
		uint32_t req_baud = (uint32_t)req.wValue * 100;
		usart4_virtual_baud = req_baud;
		if (probe_state == USB_BRIDGE_LOCKED_9600_LEGACY &&
		    req_baud == 1000000) {
			host_usb_reset_irq = false;
			probe_state = USB_BRIDGE_PROBE_WAIT_BOOT_1M;
			probe_state_enter_us = (uint32_t)get_time().val;
			probe_9600_hits = 0;
			servo_bridge_init(&bridge, /*is_epoch_leader=*/true,
					  probe_state_enter_us);
			active_usb_protocol = SERVO_PROTO_NONE;
			usb_bridge_update_effective_proto();
			usart4_configure_line(1000000, /*two_stop_bits=*/true);
			servo_frame_rx_init(&bridge.rx);
			hook_call_deferred(&bridge_arbiter_deferred_data, 0);
		} else if (probe_state == USB_BRIDGE_LOCKED_1M_BRIDGE &&
			   req_baud == 9600) {
			/* servod startup setting 9600 baud: absorb without
			 * disturbing 1M bridge */
		}
		break;
	}
	case USB_USART_BREAK:
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
		if (usart4_usb.deferred)
			hook_call_deferred(usart4_usb.deferred, 0);
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
		ep7_out_session_handled = false;
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

int msc_iface_request(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx)
{
	if (!ep0_buf_rx)
		return -1;

	if (ep0_buf_rx[0] ==
	    (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE |
	     (USB_REQ_SET_INTERFACE << 8))) {
		if (ep0_buf_rx[1] != 0)
			return -1;

		btable_ep[USB_EP_MSC].tx_count = 0;
		btable_ep[USB_EP_MSC].rx_count = usb_ep_rx_size(64);
		ep2_out_pending = 0;
		STM32_TOGGLE_EP(USB_EP_MSC,
				EP_TX_RX_MASK | EP_RX_DTOG | EP_TX_DTOG,
				EP_TX_NAK | EP_RX_VALID, EP_CTR_BOTH);
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
USB_DECLARE_IFACE(USB_IFACE_MSC, msc_iface_request);

int usb_bridge_clear_halt(uint8_t ep_addr)
{
	if (ep_addr == (USB_EP_MSC | 0x80)) {
		btable_ep[USB_EP_MSC].tx_count = 0;
		STM32_TOGGLE_EP(USB_EP_MSC, EP_TX_MASK | EP_TX_DTOG, EP_TX_NAK,
				EP_CTR_BOTH);
	} else if (ep_addr == USB_EP_MSC) {
		ep2_out_pending = 0;
		btable_ep[USB_EP_MSC].rx_count = usb_ep_rx_size(64);
		STM32_TOGGLE_EP(USB_EP_MSC, EP_RX_MASK | EP_RX_DTOG,
				EP_RX_VALID, EP_CTR_BOTH);
	} else {
		if (effective_usb_protocol == SERVO_PROTO_NONE ||
		    dppu_pulse_active)
			return -1;

		if (ep_addr == (USB_EP_BRIDGE | 0x80)) {
			btable_ep[USB_EP_BRIDGE].tx_count = 0;
			ep7_zlp_in_flight = false;
			STM32_TOGGLE_EP(USB_EP_BRIDGE, EP_TX_MASK | EP_TX_DTOG,
					EP_TX_NAK, EP_CTR_BOTH);
			if (bridge.usb_in_busy || !bridge.tx_credits ||
			    bridge.has_tx_data) {
				host_usb_reset_irq = true;
				hook_call_deferred(
					&bridge_arbiter_deferred_data, 0);
			}
		} else if (ep_addr == USB_EP_BRIDGE) {
			ep7_out_pending = 0;
			ep7_out_session_handled = false;
			btable_ep[USB_EP_BRIDGE].rx_count = usb_ep_rx_size(64);
			STM32_TOGGLE_EP(USB_EP_BRIDGE, EP_RX_MASK | EP_RX_DTOG,
					EP_RX_VALID, EP_CTR_BOTH);
			if (bridge.usb_in_busy || !bridge.tx_credits ||
			    bridge.has_tx_data) {
				host_usb_reset_irq = true;
				hook_call_deferred(
					&bridge_arbiter_deferred_data, 0);
			}
		} else {
			return -1;
		}
	}

	btable_ep[0].tx_count = 0;
	STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_RX_VALID | EP_TX_VALID, 0);
	return 0;
}
