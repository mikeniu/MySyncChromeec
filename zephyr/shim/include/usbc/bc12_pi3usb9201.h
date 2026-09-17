/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_PI3USB9201_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_PI3USB9201_H_

#include "driver/bc12/pi3usb9201_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PI3USB9201_COMPAT pericom_pi3usb9201

#define BC12_CHIP_PI3USB9201(id)        \
	{                               \
		.drv = &pi3usb9201_drv, \
	},

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_PI3USB9201_H_ */
