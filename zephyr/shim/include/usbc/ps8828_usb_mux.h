/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PS8828_USB_MUX_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PS8828_USB_MUX_H_

#include "driver/retimer/ps8828.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS8828_USB_MUX_COMPAT parade_ps8828

#define USB_MUX_CONFIG_PS8828(mux_id)                  \
	{                                              \
		USB_MUX_COMMON_FIELDS(mux_id),         \
		.driver = &ps8828_usb_retimer_driver,  \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
		.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PS8828_USB_MUX_H_ */
