/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_KB8010_USB_MUX_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_KB8010_USB_MUX_H_

#include "driver/retimer/kb8010_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KB8010_USB_MUX_COMPAT kandou_kb8010

#define KB8010_RST_GPIO(mux_id) GPIO_SIGNAL(DT_PHANDLE(mux_id, reset_pin))
#define KB8010_DP_EN_GPIO(mux_id) GPIO_SIGNAL(DT_PHANDLE(mux_id, dp_en_pin))

#define KB8010_CONTROLS_CONFIG(mux_id)                       \
	{                                                    \
		.retimer_rst_gpio = KB8010_RST_GPIO(mux_id), \
		.dp_enable_gpio = KB8010_DP_EN_GPIO(mux_id), \
	}

#define USB_MUX_KB8010_CONTROL_ARRAY(mux_id) \
	[USB_MUX_PORT(mux_id)] = KB8010_CONTROLS_CONFIG(mux_id),

#define USB_MUX_KB8010_CONTROLS_ARRAY                 \
	DT_FOREACH_STATUS_OKAY(KB8010_USB_MUX_COMPAT, \
			       USB_MUX_KB8010_CONTROL_ARRAY)

#define USB_MUX_CONFIG_KB8010(mux_id)                  \
	{                                              \
		USB_MUX_COMMON_FIELDS(mux_id),         \
		.driver = &kb8010_usb_retimer_driver,  \
		.i2c_port = I2C_PORT_BY_DEV(mux_id),   \
		.i2c_addr_flags = DT_REG_ADDR(mux_id), \
	}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_KB8010_USB_MUX_H_ */
