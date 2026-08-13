/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include "driver/charger/sgm41580.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SGM41580_CHG_COMPAT sgm_41580

#define CHG_CONFIG_SGM41580(id)                     \
	{                                          \
		.i2c_port = I2C_PORT_BY_DEV(id),   \
		.i2c_addr_flags = DT_REG_ADDR(id), \
		.drv = &sgm41580_drv,               \
	},

#ifdef __cplusplus
}
#endif
