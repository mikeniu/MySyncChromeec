/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CHARGER_CHG_SM5803_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CHARGER_CHG_SM5803_H_

#include "driver/charger/sm5803.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SM5803_CHG_COMPAT siliconmitus_sm5803

#define CHG_CONFIG_SM5803(id)                      \
	{                                          \
		.i2c_port = I2C_PORT_BY_DEV(id),   \
		.i2c_addr_flags = DT_REG_ADDR(id), \
		.drv = &sm5803_drv,                \
	},

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CHARGER_CHG_SM5803_H_ */
