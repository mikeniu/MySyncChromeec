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

int timestamp_expired(timestamp_t deadline, const timestamp_t *now)
{
	timestamp_t now_val;

	if (!now) {
		now_val = get_time();
		now = &now_val;
	}

	return ((int64_t)(now->val - deadline.val) >= 0);
}

#ifdef CONFIG_PLATFORM_EC_FINGERPRINT
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
#endif /* CONFIG_PLATFORM_EC_FINGERPRINT */

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

	ts.val = __hw_clock_source_read64();

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

	ccprintf("Time:     0x%016llx us, %4lld.%06lld s\n", t.val,
		 t.val / USEC_PER_SEC, t.val % USEC_PER_SEC);
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
	k_busy_wait(i * 1000);

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
