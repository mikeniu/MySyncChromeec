/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_DRIVERS_KEYBOARD_INPUT_GHOST_FILTER_H_
#define PLATFORM_EC_ZEPHYR_DRIVERS_KEYBOARD_INPUT_GHOST_FILTER_H_

#include <zephyr/device.h>

void input_kbd_matrix_ghost_filter_hook(const struct device *dev, int col);

#endif /* PLATFORM_EC_ZEPHYR_DRIVERS_KEYBOARD_INPUT_GHOST_FILTER_H_ */
