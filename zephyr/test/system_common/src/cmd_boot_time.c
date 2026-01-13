/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Tests for the 'boottime' EC console command introduced by the
 * CONFIG_PLATFORM_EC_CONSOLE_CMD_BOOTTIME feature.
 */

#include "console.h"
#include "system_boot_time.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

ZTEST_SUITE(console_cmd_boot_time, NULL, NULL, NULL, NULL, NULL);

/** boottime requires exactly one argument; zero args → PARAM_COUNT error. */
ZTEST(console_cmd_boot_time, test_boottime_no_args)
{
	int ret = shell_execute_cmd(get_ec_shell(), "boottime");

	zassert_equal(ret, EC_ERROR_PARAM_COUNT,
		      "Expected EC_ERROR_PARAM_COUNT, got %d", ret);
}

/** Extra arguments beyond the required one → PARAM_COUNT error. */
ZTEST(console_cmd_boot_time, test_boottime_too_many_args)
{
	int ret = shell_execute_cmd(get_ec_shell(), "boottime S0 S5");

	zassert_equal(ret, EC_ERROR_PARAM_COUNT,
		      "Expected EC_ERROR_PARAM_COUNT, got %d", ret);
}

/** An unrecognised state name → PARAM1 error. */
ZTEST(console_cmd_boot_time, test_boottime_invalid_state)
{
	int ret = shell_execute_cmd(get_ec_shell(), "boottime S3");

	zassert_equal(ret, EC_ERROR_PARAM1, "Expected EC_ERROR_PARAM1, got %d",
		      ret);
}

/**
 * After triggering the relevant boot events the command must succeed for
 * both S0 and S5 state queries.
 */
ZTEST(console_cmd_boot_time, test_boottime_s5_success)
{
	int ret;

	/* Ensure first_S5_time has been captured. */
	update_ap_boot_time(RSMRST);

	ret = shell_execute_cmd(get_ec_shell(), "boottime S5");
	zassert_equal(ret, EC_SUCCESS, "boottime S5 failed: %d", ret);
}

ZTEST(console_cmd_boot_time, test_boottime_s0_success)
{
	int ret;

	/* Ensure first_S0_time has been captured. */
	update_ap_boot_time(PLTRST_HIGH);

	ret = shell_execute_cmd(get_ec_shell(), "boottime S0");
	zassert_equal(ret, EC_SUCCESS, "boottime S0 failed: %d", ret);
}

/** Argument matching must be case-insensitive. */
ZTEST(console_cmd_boot_time, test_boottime_case_insensitive)
{
	update_ap_boot_time(RSMRST);
	update_ap_boot_time(PLTRST_HIGH);

	zassert_equal(shell_execute_cmd(get_ec_shell(), "boottime s0"),
		      EC_SUCCESS, "boottime s0 (lower-case) failed");
	zassert_equal(shell_execute_cmd(get_ec_shell(), "boottime s5"),
		      EC_SUCCESS, "boottime s5 (lower-case) failed");
}
