/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _NET_SERIAL_H
#define _NET_SERIAL_H

#include "libusb.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	libusb_device_handle *handle;
	libusb_device **devs;
	libusb_context *ctx;
	uint8_t endpoint_in;
	uint8_t endpoint_out;
} ft_usb_context_t;

#define FT_USB_DEVICE_REMOVE_MAGIC 0x55aa

int ft_usb_connect(const uint32_t pid, const uint32_t vid);

int ft_usb_reconnect(const uint32_t pid, const uint32_t vid);

int ft_usb_send(const uint8_t *buf, uint32_t len);

int ft_usb_recv_timeout(uint8_t *buf, uint32_t len, uint32_t timeout);

void ft_usb_free(void);

int ft_usb_exist(uint16_t *vid,uint16_t *pid);

int LIBUSB_CALL usb_remove_event_callback(libusb_context *ctx,
					  libusb_device *dev,
					  libusb_hotplug_event event,
					  void *user_data);

#ifdef __cplusplus
}
#endif

#endif // _NET_SERIAL_H
