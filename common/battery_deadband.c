/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery discharge deadband filter.
 */

#include "battery.h"
#include "battery_smart.h"
#include "common.h"
#include "math_util.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

LOG_MODULE_REGISTER(battery_deadband, LOG_LEVEL_INF);

#define BATTERY_DEADBAND_PRINT_INTERVAL_S 60

uint8_t batt_deadband_check(uint8_t batt_flags,
			    const struct batt_params *params)
{
	const int batt_discharge_deadband_ma =
		CONFIG_PLATFORM_EC_BATTERY_DEADBAND_MA;
	const uint8_t needed_flags = EC_BATT_FLAG_AC_PRESENT |
				     EC_BATT_FLAG_BATT_PRESENT |
				     EC_BATT_FLAG_DISCHARGING;

	if ((batt_flags & needed_flags) != needed_flags) {
		/*
		 * Preconditions not met, no override needed.
		 */
		return batt_flags;
	}
	if (!(params->status & STATUS_FULLY_CHARGED)) {
		/*
		 * Preconditions not met, no override needed.
		 */
		return batt_flags;
	}

	const int actual_current_ma = params->current;

	if (actual_current_ma >= 0 ||
	    ABS(actual_current_ma) >= batt_discharge_deadband_ma) {
		/*
		 * Battery is not discharging or discharging below
		 * deadband threshold.
		 */
		return batt_flags;
	}

	/*
	 * The battery is reporting that it's FULL and
	 * discharging/leaking a trivial amount of current
	 * due to hardware limitations. This can happen
	 * repeatedly at a fast rate but is not relevant
	 * at the system level.
	 */

	static k_timepoint_t next_print_deadline;

	if (sys_timepoint_expired(next_print_deadline)) {
		next_print_deadline = sys_timepoint_calc(
			K_SECONDS(BATTERY_DEADBAND_PRINT_INTERVAL_S));
		LOG_INF("Battery discharging %d mA (in %d mA deadband)",
			ABS(actual_current_ma), batt_discharge_deadband_ma);
	}

	batt_flags &= ~EC_BATT_FLAG_DISCHARGING;

	return batt_flags;
}
