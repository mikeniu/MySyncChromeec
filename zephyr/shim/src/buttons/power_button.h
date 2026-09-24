/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_SRC_BUTTONS_POWER_BUTTON_H_
#define PLATFORM_EC_ZEPHYR_SHIM_SRC_BUTTONS_POWER_BUTTON_H_

#include <stdint.h>

void handle_power_button(int8_t new_pin_state);

#endif /* PLATFORM_EC_ZEPHYR_SHIM_SRC_BUTTONS_POWER_BUTTON_H_ */
