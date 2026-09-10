/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_SYSTEM_BOOT_TIME_H_
#define PLATFORM_EC_INCLUDE_SYSTEM_BOOT_TIME_H_

#include "ec_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Updates ap boot time data.
 *
 * @param boot time param needs to be updated
 */
void update_ap_boot_time(enum boot_time_param param);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_SYSTEM_BOOT_TIME_H_ */
