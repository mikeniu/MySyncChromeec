/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hwtimer.h"

#include <stdint.h>

#include <zephyr/kernel.h>

uint64_t __hw_clock_source_read64(void)
{
	if (IS_ENABLED(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER)) {
		return k_cyc_to_us_floor64(k_cycle_get_64());
	}

	return k_ticks_to_us_floor64(k_uptime_ticks());
}
