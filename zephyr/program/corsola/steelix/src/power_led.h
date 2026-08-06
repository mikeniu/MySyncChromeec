/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CROS_EC_STEELIX_POWER_LED_H
#define CROS_EC_STEELIX_POWER_LED_H

#include <stdbool.h>

/**
 * Cycle power LED state on each call.
 * States: OFF → GREEN → RED → GREEN → ...
 */
void led_set_power_blink(void);

/**
 * Query whether power LED blink is currently active.
 *
 * @return true if blinking, false otherwise
 */
bool led_get_power_blink_state(void);

#endif /* CROS_EC_STEELIX_POWER_LED_H */
