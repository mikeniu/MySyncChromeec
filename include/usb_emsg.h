/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Extended message buffer */

#ifndef __CROS_EC_USB_EBUF_H
#define __CROS_EC_USB_EBUF_H

struct extended_msg {
	uint16_t rev;
	uint16_t header;
	uint32_t len;
	uint8_t buf[260];
};

/* Defined in usb_prl_sm.c */
extern struct extended_msg emsg[CONFIG_USB_PD_PORT_COUNT];

#endif /* __CROS_EC_USB_EBUF_H */
