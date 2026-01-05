/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "btn_ign_in.h"
#include "common.h"
#include "gpio.h"
#include "timer.h"

int btn_ign_in_power_pressed(int new_pressed)
{
	static timestamp_t ign_start_time;

	int ignore_us = 0;

	if (gpio_get_level(GPIO_BTN_IGN)) {
		if (new_pressed) {
			if (ign_start_time.val == 0)
				ign_start_time = get_time();

			ignore_us = 4 * SECOND - time_since32(ign_start_time);
		} else {
			ign_start_time.val = 0;
		}
	} else {
		ign_start_time.val = 0;
	}

	return ignore_us;
}
