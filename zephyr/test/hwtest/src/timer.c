/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/** @brief Test functions defined in timer.h.
 *
 * This test only validates the functionality of code in timer.h and is not
 * expected to accurately measure/check the timing.
 */

#include "common.h"
#include "timer.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(timer, NULL, NULL, NULL, NULL, NULL);

ZTEST(timer, test_crec_usleep)
{
	const int expected_duration = 12345;

#if defined(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER)
	uint64_t start_time = sys_clock_cycle_get_64();
#else
	uint64_t start_time = (uint64_t)sys_clock_cycle_get_32();
#endif
	/* This test is trying to test the functionality of crec_usleep, so we
	 * allow the usage even though it's in Zephyr code.
	 */
	crec_usleep(expected_duration); /* NOLINT_EC_SYMBOL */
#if defined(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER)
	uint64_t now = sys_clock_cycle_get_64();
#else
	uint64_t now = (uint64_t)sys_clock_cycle_get_32();
#endif
	uint64_t sleep_duration = ((now - start_time) * USEC_PER_SEC) /
		sys_clock_hw_cycles_per_sec();

	/* The sleep duration is adjusted to the system tick boundaries. */
	/* The maximum error threshold is two ticks. */
	int error_threshold =
		(USEC_PER_SEC / CONFIG_SYS_CLOCK_TICKS_PER_SEC) * 2;

	/*
	 * The SYS_CLK for NPCX is generated from a timer clocked by LFCLK which
	 * runs at 32768 Hz with an error of 2%. The SYS_CLK frequency is
	 * configurable and defaults to 10000 Hz for Zephyr. The maximum error
	 * therefore needs to be calculated based on the configured frequency
	 * (CONFIG_SYS_CLOCK_TICKS_PER_SEC) and 2% error
	 */
	if (IS_ENABLED(CONFIG_BASEBOARD_HELIPILOT)) {
		double max_error = expected_duration * 0.02;
		double clock_tick_us =
			USEC_PER_SEC / CONFIG_SYS_CLOCK_TICKS_PER_SEC;

		/* Assume a worst case error of max_error + 1 clock tick */
		error_threshold = (int)(max_error + clock_tick_us);
	}

	zassert_true(sleep_duration >= expected_duration);
	zassert_true((sleep_duration - expected_duration) < error_threshold);
}

/* When timestamp_expired is called with NULL for the second parameter,
 * get_time() should be used for the "now" value.
 */
ZTEST(timer, test_timestamp_expired)
{
	/* Set an arbitrary time for "now", all times will be relative to now */
	timestamp_t now = { .val = 2 * USEC_PER_SEC * SEC_PER_HOUR };
	timestamp_t deadline;

	/* set the deadline in the past, verify expired*/
	deadline.val = now.val - 1;
	zassert_true(timestamp_expired(deadline, &now));

	/* set the deadline in the now, verify expired*/
	deadline.val = now.val;
	zassert_true(timestamp_expired(deadline, &now));

	/* set the deadline in the future, verify not expired*/
	deadline.val = now.val + 1;
	zassert_false(timestamp_expired(deadline, &now));
}

/* When timestamp_expired is called with NULL for the second parameter,
 * get_time() should be used for the "now" value.
 */
ZTEST(timer, test_timestamp_expired_null)
{
	timestamp_t deadline;

	/* set the deadline in the past, verify expired */
	deadline.val = get_time().val - 1;
	zassert_true(timestamp_expired(deadline, NULL));

	/* set the deadline to far enough in the future that it will not expire,
	 * verify not expired */
	deadline.val = get_time().val + USEC_PER_SEC;
	zassert_false(timestamp_expired(deadline, NULL));
}
