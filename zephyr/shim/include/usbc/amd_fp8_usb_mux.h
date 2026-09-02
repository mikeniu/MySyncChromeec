/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_AMD_FP8_USB_MUX_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_AMD_FP8_USB_MUX_H_

#include "usb_mux.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AMD_FP8_USB_MUX_COMPAT amd_usbc_mux_amd_fp8

#define USB_MUX_CONFIG_AMD_FP8(mux_id)                 \
	{                                              \
		USB_MUX_COMMON_FIELDS(mux_id),         \
		.driver = &amd_fp8_usb_mux_driver,     \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
		.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_AMD_FP8_USB_MUX_H_ */
