/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola Hook function  */

#ifndef PLATFORM_EC_ZEPHYR_PROGRAM_CORSOLA_INCLUDE_ITE_HOOKS_H_
#define PLATFORM_EC_ZEPHYR_PROGRAM_CORSOLA_INCLUDE_ITE_HOOKS_H_

#include "common.h"

__override_proto void board_rt9490_adc_control(void);

#endif /* PLATFORM_EC_ZEPHYR_PROGRAM_CORSOLA_INCLUDE_ITE_HOOKS_H_ */
