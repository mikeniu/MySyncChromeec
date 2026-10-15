/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "charger_profile_override.h"
#include "common.h"
#include "console.h"
#include "test_util.h"
#include "util.h"

static const struct fast_charge_profile test_profiles[] = {
	{
		.temp_c = TEMPC_TENTHS_OF_DEG(20),
		.current_mA = { 1000, 500 },
	},
	{
		.temp_c = CHARGER_PROF_TEMP_C_LAST_RANGE,
		.current_mA = { 2000, 1500 },
	},
};

static const struct fast_charge_params test_params = {
	.total_temp_ranges = ARRAY_SIZE(test_profiles),
	.default_temp_range_profile = 0,
	.voltage_mV = { 4100, CHARGER_PROF_VOLTAGE_MV_LAST_RANGE },
	.chg_profile_info = test_profiles,
};

static int run_cmd(const char *cmd)
{
	char buf[64];

	strzcpy(buf, cmd, sizeof(buf));
	return test_send_console_command(buf);
}

test_static int test_fastcharge_test_flags(void)
{
	const struct fast_charge_profile *prev_prof = &test_profiles[0];
	struct charge_state_data curr = {
		.state = ST_CHARGE,
	};

	/*
	 * Verify clearing both bad-reading flags (0, 0) removes only
	 * BATT_FLAG_BAD_TEMPERATURE and BATT_FLAG_BAD_VOLTAGE while preserving
	 * other battery status flags.
	 */
	curr.batt.flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_WANT_CHARGE |
			  BATT_FLAG_BAD_TEMPERATURE | BATT_FLAG_BAD_VOLTAGE;
	TEST_EQ(run_cmd("fastchgtest on 0 0 20 4000"), EC_SUCCESS, "%d");
	TEST_EQ(charger_profile_override_common(&curr, &test_params, &prev_prof,
						8800),
		0, "%d");
	TEST_BITS_CLEARED(curr.batt.flags,
			  (BATT_FLAG_BAD_TEMPERATURE | BATT_FLAG_BAD_VOLTAGE));
	TEST_BITS_SET(curr.batt.flags,
		      (BATT_FLAG_RESPONSIVE | BATT_FLAG_WANT_CHARGE));
	TEST_EQ(curr.requested_current, 1000, "%d");

	/*
	 * Verify setting BATT_FLAG_BAD_TEMPERATURE (1, 0) while clearing
	 * BATT_FLAG_BAD_VOLTAGE preserves existing flags and keeps prev_prof.
	 */
	curr.batt.flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_VOLTAGE;
	TEST_EQ(run_cmd("fastchgtest on 1 0 30 4000"), EC_SUCCESS, "%d");
	TEST_EQ(charger_profile_override_common(&curr, &test_params, &prev_prof,
						8800),
		0, "%d");
	TEST_BITS_SET(curr.batt.flags,
		      (BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_TEMPERATURE));
	TEST_BITS_CLEARED(curr.batt.flags, BATT_FLAG_BAD_VOLTAGE);
	TEST_EQ(curr.requested_current, 1000, "%d");

	/*
	 * Verify clearing BATT_FLAG_BAD_TEMPERATURE and setting
	 * BATT_FLAG_BAD_VOLTAGE (0, 1) selects the highest voltage range.
	 */
	TEST_EQ(run_cmd("fastchgtest on 0 1 30 4000"), EC_SUCCESS, "%d");
	TEST_EQ(charger_profile_override_common(&curr, &test_params, &prev_prof,
						8800),
		0, "%d");
	TEST_BITS_CLEARED(curr.batt.flags, BATT_FLAG_BAD_TEMPERATURE);
	TEST_BITS_SET(curr.batt.flags,
		      (BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_VOLTAGE));
	TEST_EQ(curr.requested_current, 1500, "%d");

	TEST_EQ(run_cmd("fastchgtest off"), EC_SUCCESS, "%d");
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	RUN_TEST(test_fastcharge_test_flags);
	test_print_result();
}
