/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_RT1718S_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_RT1718S_H_

#include "tcpm/rt1718s_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT1718S_BC12_COMPAT richtek_rt1718s_bc12

#define BC12_CHIP_RT1718S(id)             \
	{                                 \
		.drv = &rt1718s_bc12_drv, \
	},

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_RT1718S_H_ */
