/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "hwtimer.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/*
 * Test the 32-bit hardware timer path of common/timer.c, which is not normally
 * compiled when CONFIG_HWTIMER_64BIT is enabled in Zephyr.
 */
#undef CONFIG_HWTIMER_64BIT
#undef CONFIG_CMD_WAITMS
#undef CONFIG_CMD_GETTIME
#undef CONFIG_CMD_TIMERINFO

#define get_time get_time_32bit
#define get_time_mock get_time_mock_32bit
#define crec_usleep crec_usleep_32bit
#define timestamp_expired timestamp_expired_32bit
#define clock clock_32bit
#define timer_print_info timer_print_info_32bit
#define timer_reset_track_packed_for_test timer_reset_track_packed_32bit
#define __hw_clock_source_read __hw_clock_source_read_32bit

timestamp_t get_time_32bit(void);
void timer_reset_track_packed_32bit(void);

static uint32_t mock_hw_timer;

/* This mock replaces the 32-bit hardware timer read */
uint32_t __hw_clock_source_read_32bit(void)
{
	return mock_hw_timer;
}

#include "common/timer.c"

ZTEST(timer, test_timer_overflow_lockfree)
{
	timestamp_t t1;
	timestamp_t t2;
	timestamp_t t3;
	timestamp_t t_stale;
	timestamp_t t4;

	/* Reset lock-free tracker state */
	clksrc_high = 0;
	mock_hw_timer = 0x00000000;
	timer_reset_track_packed_32bit();

	/* Simulate normal state near 32-bit rollover */
	mock_hw_timer = 0xFFFFFFF0;
	t1 = get_time_32bit();
	zassert_equal(t1.le.hi, 0, "hi should be 0");
	zassert_equal(t1.le.lo, 0xFFFFFFF0, "lo should be 0xFFFFFFF0");

	/*
	 * Simulate hardware timer rollover with interrupts disabled
	 * (clksrc_high not yet incremented by ISR).
	 */
	mock_hw_timer = 0x00000010;
	t2 = get_time_32bit();

	zassert_equal(t2.le.hi, 1,
		      "hi should have been incremented by lock-free logic");
	zassert_equal(t2.le.lo, 0x00000010, "lo should be 0x00000010");
	zassert_true(t2.val > t1.val, "Time must be monotonic");

	/* Simulate ISR finally running and incrementing clksrc_high */
	clksrc_high = 1;
	mock_hw_timer = 0x00000020;
	t3 = get_time_32bit();

	zassert_equal(t3.le.hi, 1, "hi should still be 1 after ISR");
	zassert_equal(t3.le.lo, 0x00000020, "lo should be 0x00000020");
	zassert_true(t3.val > t2.val, "Time must be monotonic after ISR");

	/*
	 * Simulate a preempted task resuming with a stale hi=0 tag after
	 * track_packed has already advanced to hi=1; verify it does not
	 * overwrite track_packed backwards.
	 */
	clksrc_high = 0;
	mock_hw_timer = 0xFFFFFFE0;
	t_stale = get_time_32bit();
	zassert_equal(t_stale.le.hi, 0, "stale sample keeps its sampled hi");

	clksrc_high = 1;
	mock_hw_timer = 0xFFFFFFF0;
	t3 = get_time_32bit();
	zassert_equal(t3.le.hi, 1, "hi should be 1");

	/* Roll over again with interrupts disabled while clksrc_high == 1 */
	mock_hw_timer = 0x00000010;
	t4 = get_time_32bit();
	zassert_equal(t4.le.hi, 2, "hi should increment to 2 on next rollover");
}
