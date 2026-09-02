/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PS8743_USB_MUX_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PS8743_USB_MUX_H_

#include "usb_mux/ps8743_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS8743_USB_MUX_COMPAT parade_ps8743
#define PS8743_EMUL_COMPAT zephyr_ps8743_emul

/* clang-format off */
#define USB_MUX_CONFIG_PS8743(mux_id)                  \
	{                                              \
		USB_MUX_COMMON_FIELDS(mux_id),         \
		.driver = &ps8743_usb_mux_driver,      \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
		.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}
/* clang-format on */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PS8743_USB_MUX_H_ */
