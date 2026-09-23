/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_NX20P348X_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_NX20P348X_H_

#include "ppc/nx20p348x_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NX20P348X_COMPAT nxp_nx20p348x

#define PPC_CHIP_NX20P348X(id)                                      \
	{                                                           \
		.i2c_port = I2C_PORT_BY_DEV(id),                    \
		.i2c_addr_flags = DT_REG_ADDR(id),                  \
		.drv = &nx20p348x_drv,                              \
		.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}), \
	}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_NX20P348X_H_ */
