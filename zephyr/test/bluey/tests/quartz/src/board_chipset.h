/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_BLUEY_TESTS_QUARTZ_SRC_BOARD_CHIPSET_H_
#define PLATFORM_EC_ZEPHYR_TEST_BLUEY_TESTS_QUARTZ_SRC_BOARD_CHIPSET_H_

#include "gpio.h"

void board_chipset_startup_quartz(void);
void board_chipset_shutdown_quartz(void);
void s3_power_interrupt(enum gpio_signal signal);

#endif /* PLATFORM_EC_ZEPHYR_TEST_BLUEY_TESTS_QUARTZ_SRC_BOARD_CHIPSET_H_ */
