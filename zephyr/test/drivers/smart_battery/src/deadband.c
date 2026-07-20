/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "zephyr/kernel.h"

#include <zephyr/ztest.h>

ZTEST_USER(battery_deadband, test_deadband_suppresses_discharging_flag)
{
	struct batt_params params = {
		.is_present = BP_YES,
		.status = STATUS_FULLY_CHARGED,
		/* Inside deadband */
		.current = -(CONFIG_PLATFORM_EC_BATTERY_DEADBAND_MA - 1),
	};

	battery_set_dynamic_info(&params, true, false, false);
	zassert_equal(0, battery_dynamic[BATT_IDX_MAIN].flags &
				 EC_BATT_FLAG_DISCHARGING);
}

ZTEST_USER(battery_deadband,
	   test_deadband_allows_discharging_flag_outside_deadband)
{
	struct batt_params params = {
		.is_present = BP_YES,
		.status = STATUS_FULLY_CHARGED,
		/* Outside deadband */
		.current = -(CONFIG_PLATFORM_EC_BATTERY_DEADBAND_MA + 5),
	};

	battery_set_dynamic_info(&params, true, false, false);
	zassert_not_equal(0, battery_dynamic[BATT_IDX_MAIN].flags &
				     EC_BATT_FLAG_DISCHARGING);
}

ZTEST_USER(battery_deadband, test_deadband_not_applied_when_not_full)
{
	struct batt_params params = {
		.is_present = BP_YES,
		.status = 0, /* Not fully charged */
		/* Inside deadband, but not full */
		.current = -(CONFIG_PLATFORM_EC_BATTERY_DEADBAND_MA - 1),
	};

	battery_set_dynamic_info(&params, true, false, false);
	zassert_not_equal(0, battery_dynamic[BATT_IDX_MAIN].flags &
				     EC_BATT_FLAG_DISCHARGING);
}

ZTEST_USER(battery_deadband, test_deadband_not_applied_when_no_ac)
{
	struct batt_params params = {
		.is_present = BP_YES,
		.status = STATUS_FULLY_CHARGED,
		/* Inside deadband, but no AC */
		.current = -(CONFIG_PLATFORM_EC_BATTERY_DEADBAND_MA - 1),
	};

	battery_set_dynamic_info(&params, false, false, false);
	zassert_not_equal(0, battery_dynamic[BATT_IDX_MAIN].flags &
				     EC_BATT_FLAG_DISCHARGING);
}

ZTEST_SUITE(battery_deadband, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
