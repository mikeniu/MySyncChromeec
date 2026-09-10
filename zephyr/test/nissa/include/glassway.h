/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_NISSA_INCLUDE_GLASSWAY_H_
#define PLATFORM_EC_ZEPHYR_TEST_NISSA_INCLUDE_GLASSWAY_H_

#include "ec_commands.h"

void fan_init(void);
void board_setup_init(void);
void alt_sensor_init(void);
void kb_init(void);

extern enum glassway_sub_board_type glassway_cached_sub_board;

#endif /* PLATFORM_EC_ZEPHYR_TEST_NISSA_INCLUDE_GLASSWAY_H_ */
