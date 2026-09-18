/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_RT9490_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_RT9490_H_

#include "driver/charger/rt9490.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT9490_BC12_COMPAT richtek_rt9490

#define BC12_CHIP_RT9490(id)             \
	{                                \
		.drv = &rt9490_bc12_drv, \
	},

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_RT9490_H_ */
