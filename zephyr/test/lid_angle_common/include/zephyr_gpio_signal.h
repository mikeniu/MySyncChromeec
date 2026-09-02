/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file Since we don't actually have any GPIOs, just define the blank enum.
 */

#ifndef ZEPHYR_TEST_LID_ANGLE_COMMON_INCLUDE_ZEPHYR_GPIO_SIGNAL_H_
#define ZEPHYR_TEST_LID_ANGLE_COMMON_INCLUDE_ZEPHYR_GPIO_SIGNAL_H_

enum gpio_signal {
	GPIO_COUNT,
	GPIO_LIMIT = 0x0FFF,
};

#endif /* ZEPHYR_TEST_LID_ANGLE_COMMON_INCLUDE_ZEPHYR_GPIO_SIGNAL_H_ */
