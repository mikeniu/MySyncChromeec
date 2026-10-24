/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/** @brief Test functions defined in timer.h, like crec_usleep().
 *
 * This test only validates the functionality of code in timer.h and is not
 * expected to accurately measure/check the timing.
 */

#include "common.h"
#include "compiler.h"
#ifdef BOARD_HOST
#include "hwtimer.h"
#include "mock/hwtimer_mock.h"
#endif
#include "math_util.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "uart.h"
#include "watchdog.h"

test_static int test_usleep(void)
{
	constexpr int expected_duration = 12345;

	timestamp_t start_time = get_time();
	crec_usleep(expected_duration);
	int sleep_duration = time_since32(start_time);
	int error_threshold = 100;

	/* Helipilot uses the LFCLK for events which runs at 32768 Hz with an
	 * error of 2%. This gives a 30.5 us resolution and a max error of 246.9
	 * us on 12345 us. This is considerably lower resolution and higher
	 * error than the stm32 boards and may result in higher deltas.
	 */
	if (IS_ENABLED(BASEBOARD_HELIPILOT)) {
		double max_error = expected_duration * 0.02;
		double clock_tick_us = (1.0 / 32768.0) * 1000000.0;

		/* Assume a worst case error of max_error + 1 clock tick */
		error_threshold = static_cast<int>(max_error + clock_tick_us);
	}

	TEST_NEAR(expected_duration, sleep_duration, error_threshold, "%d");

	return EC_SUCCESS;
}

/* When timestamp_expired is called with NULL for the second parameter,
 * get_time() should be used for the "now" value.
 */
test_static int test_timestamp_expired(void)
{
	/* Set an arbitrary time for "now", all times will be relative to now */
	timestamp_t now = { .val = 2 * HOUR };
	timestamp_t deadline;

	/* set the deadline in the past, verify expired*/
	deadline.val = now.val - 1;
	TEST_ASSERT(timestamp_expired(deadline, &now));

	/* set the deadline in the now, verify expired*/
	deadline.val = now.val;
	TEST_ASSERT(timestamp_expired(deadline, &now));

	/* set the deadline in the future, verify not expired*/
	deadline.val = now.val + 1;
	TEST_ASSERT(!timestamp_expired(deadline, &now));

	return EC_SUCCESS;
}

/* When timestamp_expired is called with NULL for the second parameter,
 * get_time() should be used for the "now" value.
 */
test_static int test_timestamp_expired_null(void)
{
	timestamp_t deadline;

	/* set the deadline in the past, verify expired */
	deadline.val = get_time().val - 1;
	TEST_ASSERT(timestamp_expired(deadline, NULL));

	/* set the deadline to far enough in the future that it will not expire,
	 * verify not expired */
	deadline.val = get_time().val + SECOND;
	TEST_ASSERT(!timestamp_expired(deadline, NULL));

	return EC_SUCCESS;
}

/* When timestamp_expired is called from an interrupt or when interrupts
 * are disabled it should call into the delay function instead and print
 * a warning. The rate of the warnings should be throttled to avoid
 * filling the uart buffer.
 */
test_static int test_usleep_warning(void)
{
	const int usleep_warning_interval_ms = (20 * MSEC);

	/* Skip this test if the COMMON_TIMER module isn't being used, as the
	 * warning isn't printed */
	if (IS_ENABLED(CONFIG_COMMON_TIMER)) {
		int32_t pre_test_buffer_used;
		int32_t delta_buffer_used;

		/* use the delta_buffer_used to determine if the printing
		 * occurred */
		interrupt_disable();
		pre_test_buffer_used = uart_buffer_used();
		crec_usleep(1);
		delta_buffer_used = uart_buffer_used() - pre_test_buffer_used;
		TEST_GE(delta_buffer_used,
			(int32_t)strlen("Sleeping not allowed"), "%d");
		interrupt_enable();

		/* calling usleep again immediately shouldn't print because the
		 * timer didn't expire
		 */
		interrupt_disable();
		pre_test_buffer_used = uart_buffer_used();
		crec_usleep(1);
		delta_buffer_used = uart_buffer_used() - pre_test_buffer_used;
		TEST_EQ(delta_buffer_used, 0, "%d");
		interrupt_enable();

		/* Calling usleep after a sufficient delay should print again */
		udelay(usleep_warning_interval_ms);
		interrupt_disable();
		pre_test_buffer_used = uart_buffer_used();
		crec_usleep(1);
		delta_buffer_used = uart_buffer_used() - pre_test_buffer_used;
		TEST_GE(delta_buffer_used,
			(int32_t)strlen("Sleeping not allowed"), "%d");
		interrupt_enable();
	}

	return EC_SUCCESS;
}

#ifdef BOARD_HOST
test_static int test_get_time_overflow_irq_disabled()
{
	timestamp_t init_ts = { .val = UINT64_C(0x000000017fffffff) };

	mock_ctrl_hwtimer.auto_increment = false;
	timer_init();

	/* Start in lower half, just before upper half */
	force_time(init_ts);
	TEST_EQ(get_time().val, init_ts.val, "0x%016" PRIx64);

	/*
	 * Transition to the exact upper-half boundary (BIT(31) set), verifying
	 * that get_time() updates clksrc_msb_epoch directly.
	 */
	mock_ctrl_hwtimer.clock_val = BIT(31);
	TEST_EQ(get_time().val, UINT64_C(0x0000000180000000), "0x%016" PRIx64);

	mock_ctrl_hwtimer.clock_val = 0xfffffff0;
	TEST_EQ(get_time().val, UINT64_C(0x00000001fffffff0), "0x%016" PRIx64);

	/*
	 * Simulate 32-bit hardware counter wrapping to exact 0x00000000 and
	 * 0x00000010 while interrupts are disabled (before process_timers(1)
	 * can run and increment clksrc_high).
	 */
	interrupt_disable();
	mock_ctrl_hwtimer.clock_val = 0x00000000;
	TEST_EQ(get_time().val, UINT64_C(0x0000000200000000), "0x%016" PRIx64);

	mock_ctrl_hwtimer.clock_val = 0x00000010;
	TEST_EQ(get_time().val, UINT64_C(0x0000000200000010), "0x%016" PRIx64);

	mock_ctrl_hwtimer.clock_val = 0x00000020;
	TEST_EQ(get_time().val, UINT64_C(0x0000000200000020), "0x%016" PRIx64);
	interrupt_enable();

	/*
	 * Once the timer overflow ISR runs process_timers(1), clksrc_high
	 * advances to 2 and must not double-increment.
	 */
	mock_ctrl_hwtimer.clock_val = 0x00000030;
	process_timers(1);
	TEST_EQ(get_time().val, UINT64_C(0x0000000200000030), "0x%016" PRIx64);

	/*
	 * force_time() into the upper half (BIT(31) set) must arm
	 * clksrc_msb_epoch itself, so a wrap with interrupts disabled is
	 * caught without an intervening get_time() call in the upper half.
	 */
	init_ts.val = UINT64_C(0x0000000280000000);
	force_time(init_ts);
	interrupt_disable();
	mock_ctrl_hwtimer.clock_val = 0x00000000;
	TEST_EQ(get_time().val, UINT64_C(0x0000000300000000), "0x%016" PRIx64);
	interrupt_enable();
	process_timers(1);
	TEST_EQ(get_time().val, UINT64_C(0x0000000300000000), "0x%016" PRIx64);

	mock_ctrl_hwtimer.auto_increment = true;
	return EC_SUCCESS;
}
#endif /* BOARD_HOST */

void run_test(int argc, const char **argv)
{
	test_reset();
	watchdog_reload();

	RUN_TEST(test_usleep);
	RUN_TEST(test_timestamp_expired);
	RUN_TEST(test_timestamp_expired_null);
	RUN_TEST(test_usleep_warning);
#ifdef BOARD_HOST
	RUN_TEST(test_get_time_overflow_irq_disabled);
#endif

	test_print_result();
}
