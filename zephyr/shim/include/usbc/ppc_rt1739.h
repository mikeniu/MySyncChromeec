/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_RT1739_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_RT1739_H_

#include "driver/ppc/rt1739.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT1739_PPC_COMPAT richtek_rt1739_ppc
#define RT1739_PPC_EMUL_COMPAT zephyr_rt1739_emul

#define PPC_CHIP_RT1739(id)                                               \
	{                                                                 \
		.i2c_port = I2C_PORT_BY_DEV(id),                          \
		.i2c_addr_flags = DT_REG_ADDR(id),                        \
		.drv = &rt1739_ppc_drv,                                   \
		.frs_en = COND_CODE_1(                                    \
			DT_NODE_HAS_PROP(id, frs_en_gpio),                \
			(GPIO_SIGNAL(DT_PHANDLE(id, frs_en_gpio))), (0)), \
		.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}),       \
	}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_RT1739_H_ */
