/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PS8802_USB_MUX_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PS8802_USB_MUX_H_

#include "driver/retimer/ps8802.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS8802_USB_MUX_COMPAT parade_ps8802

#define USB_MUX_CONFIG_PS8802(mux_id)                  \
	{                                              \
		USB_MUX_COMMON_FIELDS(mux_id),         \
		.driver = &ps8802_usb_mux_driver,      \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
		.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PS8802_USB_MUX_H_ */
