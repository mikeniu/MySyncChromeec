/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "watchdog.h"

int command_watchdoginfo(int argc, const char **argv);

static int test_watchdog_info_host_command(void)
{
	struct ec_params_hostcmd_watchdog_info p;
	struct ec_response_hostcmd_watchdog_info r;
	int rv;

	/* Reset stats */
	p.reset_stats = 1;
	rv = test_send_host_command(EC_CMD_HOSTCMD_WATCHDOG_INFO, 0, &p,
				    sizeof(p), &r, sizeof(r));
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	/* Verify static values */
	TEST_EQ(r.watchdog_period_ms, CONFIG_WATCHDOG_PERIOD_MS, "%d");
	TEST_EQ(r.watchdog_warning_period_ms, CONFIG_AUX_TIMER_PERIOD_MS, "%d");
	TEST_EQ(r.watchdog_reload_period_nominal_ms, HOOK_TICK_INTERVAL / MSEC,
		"%d");

	/* Get stats again and verify dynamic values are 0 */
	rv = test_send_host_command(EC_CMD_HOSTCMD_WATCHDOG_INFO, 0, &p,
				    sizeof(p), &r, sizeof(r));
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_EQ(r.watchdog_reload_period_max_ms, (int32_t)0, "%d");
	TEST_EQ(r.watchdog_reload_period_max_ts_ms, (int64_t)0, "%ld");
	TEST_EQ(r.watchdog_reload_count, (uint32_t)0, "%u");
	TEST_EQ(r.watchdog_stats_elapsed_ms, (int64_t)0, "%ld");

	/* Trigger a reload */
	udelay(HOOK_TICK_INTERVAL);
	watchdog_reload();

	/* Get stats and verify count incremented */
	p.reset_stats = 0;
	rv = test_send_host_command(EC_CMD_HOSTCMD_WATCHDOG_INFO, 0, &p,
				    sizeof(p), &r, sizeof(r));
	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_EQ(r.watchdog_reload_count, 1, "%u");
	TEST_GT(r.watchdog_stats_elapsed_ms, (int64_t)0, "%ld");
	TEST_GT(r.watchdog_reload_period_max_ms, (int32_t)0, "%d");
	TEST_GT(r.watchdog_reload_period_max_ts_ms, (int64_t)0, "%ld");

	return EC_SUCCESS;
}

static int test_watchdog_info_console_command(void)
{
	const char *out;

	/* Run the console command */
	test_capture_console(1);
	TEST_EQ(command_watchdoginfo(1, (const char *[]){ "watchdoginfo" }),
		EC_SUCCESS, "%d");
	test_capture_console(0);
	out = test_get_captured_console();
	TEST_ASSERT(strstr(out, "Watchdog Info:"));
	TEST_ASSERT(strstr(out, "Period: "));
	TEST_ASSERT(strstr(out, "Reload Count: "));
	TEST_ASSERT(strstr(out, "Reload Period Nominal: "));
	TEST_ASSERT(strstr(out, "Stats Elapsed Time: "));
	TEST_ASSERT(strstr(out, "Reload Period Max: "));
	TEST_ASSERT(strstr(out, "Reload Period Avg: "));

	/* Reset stats */
	test_capture_console(1);
	TEST_EQ(command_watchdoginfo(2, (const char *[]){ "watchdoginfo",
							  "reset_stats" }),
		EC_SUCCESS, "%d");
	test_capture_console(0);
	out = test_get_captured_console();
	TEST_ASSERT(strstr(out, "Watchdog stats reset."));

	/* Test invalid arg */
	TEST_EQ(command_watchdoginfo(2, (const char *[]){ "watchdoginfo",
							  "invalid" }),
		EC_ERROR_PARAM1, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_watchdog_info_host_command);
	RUN_TEST(test_watchdog_info_console_command);

	test_print_result();
}
