/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module for Chrome EC operating system */

#include "common.h"
#include "console.h"
#include "hwtimer.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#include <zephyr/kernel.h> /* For k_usleep() */

/* High 32-bits of the 64-bit timestamp counter. */
STATIC_IF_NOT(CONFIG_HWTIMER_64BIT) volatile uint32_t clksrc_high;

/*
 * Packed 32-bit lock-free tracker (8-bit clksrc_high tag | 24-bit lo >> 8)
 * used to detect 32-bit hardware timer rollovers while interrupts are disabled.
 */
STATIC_IF_NOT(CONFIG_HWTIMER_64BIT) uint32_t track_packed;

#if defined(CONFIG_ZTEST) || defined(TEST_BUILD)
void timer_reset_track_packed_for_test(void)
{
	__atomic_store_n(&track_packed, 0, __ATOMIC_RELAXED);
}
#endif

int timestamp_expired(timestamp_t deadline, const timestamp_t *now)
{
	timestamp_t now_val;

	if (!now) {
		now_val = get_time();
		now = &now_val;
	}

	return ((int64_t)(now->val - deadline.val) >= 0);
}

/*
 * For us < (2^31 - task scheduling latency)(~ 2147 sec), this function will
 * sleep for at least us, and no more than 2*us. As us approaches 2^32-1, the
 * probability of delay longer than 2*us (and possibly infinite delay)
 * increases.
 */
int crec_usleep(unsigned int us)
{
	/* If a wait is 0, return immediately. */
	if (!us) {
		return 0;
	}

	while (us) {
		us = k_usleep(us);
	}
	return 0;
}

#ifdef CONFIG_ZTEST
timestamp_t *get_time_mock;
#endif /* CONFIG_ZTEST */

timestamp_t get_time(void)
{
	timestamp_t ts;

#ifdef CONFIG_ZTEST
	if (get_time_mock != NULL)
		return *get_time_mock;
#endif /* CONFIG_ZTEST */

	if (IS_ENABLED(CONFIG_HWTIMER_64BIT)) {
		ts.val = __hw_clock_source_read64();
	} else {
		ts.le.hi = clksrc_high;
		ts.le.lo = __hw_clock_source_read();
		/*
		 * TODO(b/213342294) If statement below doesn't catch overflows
		 * when interrupts are disabled or currently processed interrupt
		 * has higher priority.
		 */
		if (ts.le.hi != clksrc_high) {
			ts.le.hi = clksrc_high;
			ts.le.lo = __hw_clock_source_read();
		}

		/* Lock-free timer overflow detection for b/213342294 */
		uint32_t cur_hi_tag = (ts.le.hi & 0xFFU) << 24;
		uint32_t cur_lo_val = ts.le.lo >> 8;
		uint32_t cur_packed = cur_hi_tag | cur_lo_val;

		uint32_t prev_packed =
			__atomic_load_n(&track_packed, __ATOMIC_RELAXED);
		uint32_t prev_hi_tag = prev_packed & 0xFF000000U;
		uint32_t prev_lo_val = prev_packed & 0x00FFFFFFU;

		if (prev_hi_tag == cur_hi_tag) {
			if (prev_lo_val > cur_lo_val &&
			    (prev_lo_val - cur_lo_val) > 0x00400000U) {
				ts.le.hi++;
			} else if (cur_lo_val > prev_lo_val) {
				__atomic_compare_exchange_n(
					&track_packed, &prev_packed, cur_packed,
					0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
			}
		} else if ((int8_t)((cur_hi_tag - prev_hi_tag) >> 24) > 0) {
			__atomic_compare_exchange_n(&track_packed, &prev_packed,
						    cur_packed, 0,
						    __ATOMIC_RELAXED,
						    __ATOMIC_RELAXED);
		}
	}

	return ts;
}

clock_t clock(void)
{
	/* __hw_clock_source_read() returns a microsecond resolution timer.*/
	return (clock_t)__hw_clock_source_read() / 1000;
}

/*
 * Define a 32-bit version of __hw_clock_source_read that wraps the 64-bit
 * version for chips with CONFIG_HWTIMER_64BIT.
 */
#ifdef CONFIG_HWTIMER_64BIT
__overridable uint32_t __hw_clock_source_read(void)
{
	return (uint32_t)__hw_clock_source_read64();
}
#endif /* CONFIG_HWTIMER_64BIT */

void timer_print_info(void)
{
	timestamp_t t = get_time();
	uint64_t deadline = (uint64_t)t.le.hi << 32 | __hw_clock_event_get();
	uint64_t delta = deadline - t.val;

	ccprintf("Time:     0x%016llx us, %4lld.%06lld s\n"
		 "Deadline: 0x%016llx -> %4lld.%06lld s from now\n",
		 t.val, t.val / USEC_PER_SEC, t.val % USEC_PER_SEC, deadline,
		 delta / USEC_PER_SEC, delta % USEC_PER_SEC);
	cflush();
}

#ifdef CONFIG_CMD_WAITMS
static int command_wait(int argc, const char **argv)
{
	char *e;
	int i;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	i = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	if (i < 0)
		return EC_ERROR_PARAM1;

	/*
	 * Reload the watchdog so that issuing multiple small waitms commands
	 * quickly one after the other will not cause a reset.
	 *
	 * Reloading before waiting also allows for testing watchdog.
	 */
	watchdog_reload();

	/*
	 * Waiting for too long (e.g. 3s) will cause the EC to reset due to a
	 * watchdog timeout. This is intended behaviour and is in fact used by
	 * a FAFT test to check that the watchdog timer is working.
	 */
	udelay(i * 1000);

	return EC_SUCCESS;
}
/* Typically a large delay (e.g. 3s) will cause a reset */
DECLARE_CONSOLE_COMMAND(waitms, command_wait, "msec",
			"Busy-wait for msec (large delays will reset)");
#endif

#ifdef CONFIG_CMD_GETTIME
static int command_get_time(int argc, const char **argv)
{
	timestamp_t ts = get_time();
	ccprintf("Time: 0x%016llx = %lld.%06lld s\n", ts.val,
		 ts.val / USEC_PER_SEC, ts.val % USEC_PER_SEC);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(gettime, command_get_time, NULL,
			     "Print current time");
#endif

#ifdef CONFIG_CMD_TIMERINFO
static int command_timer_info(int argc, const char **argv)
{
	timer_print_info();

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(timerinfo, command_timer_info, NULL,
			     "Print timer info");
#endif
