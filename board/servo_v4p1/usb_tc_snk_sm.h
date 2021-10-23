/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_TC_SNK_SM_H
#define __CROS_EC_USB_TC_SNK_SM_H

/* Init USBC Sink only state machine */
int usb_tc_snk_sm_init(void);

/* Run USBC Sink only state machine */
void usb_tc_snk_sm_run(void);

#endif /* __CROS_EC_USB_TC_SNK_SM_H */
