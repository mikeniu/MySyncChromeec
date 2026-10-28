/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "console.h"
#include "test/drivers/test_state.h"
#include "timer.h"

#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_HWTIMER_64BIT),
	     "Tests expect the 64-bit HW timer");

ZTEST(timer, test_console_cmd_gettime)
{
	/* Should print the current time */
	timestamp_t fake_time;
	const char *outbuffer;
	size_t buffer_size;

	/* Make get_time() return a mocked value */
	fake_time.val = 100;
	get_time_mock = &fake_time;

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "gettime"), NULL);
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_not_null(strstr(outbuffer,
				"Time: 0x0000000000000064 = 0.000100 s"),
			 "Actual: '%s'", outbuffer);
}

ZTEST(timer, test_console_cmd_timerinfo)
{
	/* Prints current time and info on running timers. */
	timestamp_t fake_time;
	const char *outbuffer;
	size_t buffer_size;

	/* Make get_time() return a mocked value */
	fake_time.val = 100;
	get_time_mock = &fake_time;

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "timerinfo"), NULL);
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_not_null(
		strstr(outbuffer,
		       "Time:     0x0000000000000064 us,    0.000100 s"),
		"Actual: '%s'", outbuffer);

	/* Task timer information not printed here if running in a Zephyr EC */
}

ZTEST(timer, test_get_time_microsecond_resolution)
{
	if (!IS_ENABLED(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER)) {
		ztest_test_skip();
		return;
	}

	const uint32_t wait_us = 10;
	const uint32_t tolerance_us = 2;
	timestamp_t start = get_time();

	k_busy_wait(wait_us);

	zassert_between_inclusive(time_since32(start), wait_us,
				  wait_us + tolerance_us);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	/* Disable get_time() mocked return value */
	get_time_mock = NULL;
}

ZTEST_SUITE(timer, drivers_predicate_post_main, NULL, reset, reset, NULL);
