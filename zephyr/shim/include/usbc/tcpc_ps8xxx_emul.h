/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_TCPC_PS8XXX_EMUL_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_TCPC_PS8XXX_EMUL_H_

#include "driver/tcpm/ps8xxx_public.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS8XXX_EMUL_COMPAT cros_ps8xxx_emul

/* clang-format off */
#define TCPC_CONFIG_PS8XXX_EMUL(id)                                            \
	{                                                                      \
		.bus_type = EC_BUS_TYPE_I2C,                                   \
		.i2c_info = {                                                  \
			.port = I2C_PORT_BY_DEV(id),                           \
			.addr_flags = DT_REG_ADDR(id),                         \
		},                                                             \
		.drv = &ps8xxx_tcpm_drv,                                       \
		COND_CODE_1(CONFIG_PLATFORM_EC_TCPC_INTERRUPT,                 \
			(.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}),   \
			 .rst_gpio = GPIO_DT_SPEC_GET_OR(id, rst_gpios, {})),  \
			(.alert_signal = COND_CODE_1(                          \
				DT_NODE_HAS_PROP(id, int_pin),                 \
				(GPIO_SIGNAL(DT_PHANDLE(id, int_pin))),        \
				(GPIO_LIMIT)))),                               \
	}
/* clang-format on */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_TCPC_PS8XXX_EMUL_H_ */
