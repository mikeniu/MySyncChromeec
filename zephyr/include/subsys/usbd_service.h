/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_SUBSYS_USBD_SERVICE_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_SUBSYS_USBD_SERVICE_H_

/**
 * Checks whether the USB device controller is suspended.
 *
 * @return true if suspended, false otherwise
 */
bool usb_is_suspended(void);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_SUBSYS_USBD_SERVICE_H_ */
