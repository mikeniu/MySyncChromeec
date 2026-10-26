/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USB_BRIDGE_H
#define __CROS_EC_USB_BRIDGE_H

#include "consumer.h"
#include "producer.h"
#include "queue.h"
#include "usb-stream.h"
#include "usb_hw.h"

/*
 * Shared queues and USB stream config defined in board.c (non-static
 * for binding).
 */
struct usart_config;
extern struct usart_config const usart4;
extern struct usb_stream_config const usart4_usb;
extern struct queue const usart4_to_bridge;
extern struct queue const bridge_to_usart4;
extern struct queue const usb_to_bridge_kbd;
extern struct queue const bridge_kbd_to_usb;

/* Consumers and producers exported to board.c for queue wiring */
extern struct consumer const usb_bridge_uart_consumer;
extern struct producer const usb_bridge_uart_producer;
extern struct consumer const usb_bridge_kbd_consumer;
extern struct producer const usb_bridge_kbd_producer;

/* Initialize packet arbiter queues, FSM state, and descriptors */
void usb_bridge_init(void);

enum usb_bridge_probe_state {
	USB_BRIDGE_PROBE_RESET_HELD = 0,
	USB_BRIDGE_PROBE_WAIT_BOOT_1M,
	USB_BRIDGE_PROBE_9600,
	USB_BRIDGE_PROBE_ACTIVE_1M,
	USB_BRIDGE_LOCKED_9600_LEGACY,
	USB_BRIDGE_LOCKED_1M_BRIDGE,
};

enum usb_bridge_probe_state usb_bridge_get_probe_state(void);
void usb_bridge_notify_atmel_reset(int released);

/* Interface request handler for USART4 stream (absorbs baud changes) */
int usart4_iface_request(usb_uint *rx_buf, usb_uint *tx_buf);

/* Interface request handler for USB_IFACE_BRIDGE */
int bridge_iface_request(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx);

/* Clear endpoint halt request handler for USB_EP_BRIDGE */
int usb_bridge_clear_halt(uint8_t ep_addr);

#endif /* __CROS_EC_USB_BRIDGE_H */
