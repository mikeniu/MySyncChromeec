/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_AOZ1380_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_AOZ1380_H_

#include "ppc/aoz1380_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AOZ1380_COMPAT aoz_aoz1380

/* Note: This chip has no i2c interface */
#define PPC_CHIP_AOZ1380(id)                                        \
	{                                                           \
		.drv = &aoz1380_drv,                                \
		.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}), \
	}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_PPC_AOZ1380_H_ */
