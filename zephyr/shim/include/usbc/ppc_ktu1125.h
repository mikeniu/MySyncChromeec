/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_KTU1125_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_KTU1125_H_

#include "ppc/ktu1125_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KTU1125_COMPAT kinetic_ktu1125

#define PPC_CHIP_KTU1125(id)                                        \
	{                                                           \
		.i2c_port = I2C_PORT_BY_DEV(id),                    \
		.i2c_addr_flags = DT_REG_ADDR(id),                  \
		.drv = &ktu1125_drv,                                \
		.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}), \
	}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_KTU1125_H_ */
