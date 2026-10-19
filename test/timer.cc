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
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "math_util.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "uart.h"
#include "watchdog.h"

#ifdef BOARD_HOST
extern "C" {
static uint32_t mock_hw_clock_val;

static void mock_hw_clock_event_set(uint32_t deadline)
{
}

static uint32_t mock_hw_clock_event_get(void)
{
	return 0;
}

static void mock_hw_clock_event_clear(void)
{
}

static uint32_t mock_hw_clock_source_read(void)
{
	return mock_hw_clock_val;
}

static void mock_hw_clock_source_set(uint32_t ts)
{
	mock_hw_clock_val = ts;
}

static int mock_hw_clock_source_init(uint32_t start_t)
{
	mock_hw_clock_val = start_t;
	return 0;
}

static void mock_task_trigger_irq(int irq)
{
}

timestamp_t common_get_time(void);
void common_force_time(timestamp_t ts);
void common_udelay(unsigned int us);
void common_timer_print_info(void);

#define __hw_clock_event_set mock_hw_clock_event_set
#define __hw_clock_event_get mock_hw_clock_event_get
#define __hw_clock_event_clear mock_hw_clock_event_clear
#define __hw_clock_source_read mock_hw_clock_source_read
#define __hw_clock_source_set mock_hw_clock_source_set
#define __hw_clock_source_init mock_hw_clock_source_init
#define task_trigger_irq mock_task_trigger_irq
#define process_timers common_process_timers
#define timestamp_expired common_timestamp_expired
#define udelay common_udelay
#define timer_arm common_timer_arm
#define timer_cancel common_timer_cancel
#define crec_usleep common_crec_usleep
#define get_time common_get_time
#define clock common_clock
#define force_time common_force_time
#define timer_print_info common_timer_print_info
#define timer_init common_timer_init
#undef DECLARE_HOOK
#define DECLARE_HOOK(hook, fn, prio)
#undef CONFIG_CMD_WAITMS
#undef CONFIG_CMD_FORCETIME
#undef CONFIG_CMD_GETTIME
#undef CONFIG_CMD_TIMERINFO

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wunused-function"
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-volatile"
#else
#pragma GCC diagnostic ignored "-Wvolatile"
#endif
#include "../common/timer.c"
#pragma GCC diagnostic pop

#undef __hw_clock_event_set
#undef __hw_clock_event_get
#undef __hw_clock_event_clear
#undef __hw_clock_source_read
#undef __hw_clock_source_set
#undef __hw_clock_source_init
#undef task_trigger_irq
#undef process_timers
#undef timestamp_expired
#undef udelay
#undef timer_arm
#undef timer_cancel
#undef crec_usleep
#undef get_time
#undef clock
#undef force_time
#undef timer_print_info
#undef timer_init
}
#endif /* BOARD_HOST */

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
test_static int test_get_time_overflow_irq_disabled(void)
{
	timestamp_t init_ts = { .val = 0x000000017fffffffULL };

	common_timer_init();

	/* Start in lower half, just before upper half */
	common_force_time(init_ts);
	TEST_EQ(common_get_time().val, (uint64_t)0x000000017fffffffULL,
		"0x%016" PRIx64);

	/*
	 * Transition to the exact upper-half boundary (BIT(31) set), verifying
	 * that common_get_time() updates clksrc_msb_epoch directly.
	 */
	mock_hw_clock_val = BIT(31);
	TEST_EQ(common_get_time().val, (uint64_t)0x0000000180000000ULL,
		"0x%016" PRIx64);

	mock_hw_clock_val = 0xfffffff0;
	TEST_EQ(common_get_time().val, (uint64_t)0x00000001fffffff0ULL,
		"0x%016" PRIx64);

	/*
	 * Simulate 32-bit hardware counter wrapping to exact 0x00000000 and
	 * 0x00000010 while interrupts are disabled (before process_timers(1)
	 * can run and increment clksrc_high).
	 */
	interrupt_disable();
	mock_hw_clock_val = 0x00000000;
	TEST_EQ(common_get_time().val, (uint64_t)0x0000000200000000ULL,
		"0x%016" PRIx64);

	mock_hw_clock_val = 0x00000010;
	TEST_EQ(common_get_time().val, (uint64_t)0x0000000200000010ULL,
		"0x%016" PRIx64);

	mock_hw_clock_val = 0x00000020;
	TEST_EQ(common_get_time().val, (uint64_t)0x0000000200000020ULL,
		"0x%016" PRIx64);
	interrupt_enable();

	/*
	 * Once the timer overflow ISR runs process_timers(1), clksrc_high
	 * advances to 2 and must not double-increment.
	 */
	mock_hw_clock_val = 0x00000030;
	common_process_timers(1);
	TEST_EQ(common_get_time().val, (uint64_t)0x0000000200000030ULL,
		"0x%016" PRIx64);

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
