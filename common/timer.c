/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module for Chrome EC operating system */

#include "atomic.h"
#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#include <zephyr/kernel.h> /* For k_usleep() */

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

#define TIMER_SYSJUMP_TAG 0x4d54 /* "TM" */

#define USLEEP_WARNING_INTERVAL_MS (20 * MSEC)

/* High 32-bits of the 64-bit timestamp counter. */
STATIC_IF_NOT(CONFIG_HWTIMER_64BIT) volatile uint32_t clksrc_high;

/* Hardware timer routine IRQ number */
static int timer_irq;

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
	}

	return ts;
}

clock_t clock(void)
{
	/* __hw_clock_source_read() returns a microsecond resolution timer.*/
	return (clock_t)__hw_clock_source_read() / 1000;
}

void force_time(timestamp_t ts)
{
	if (IS_ENABLED(CONFIG_HWTIMER_64BIT)) {
		__hw_clock_source_set64(ts.val);
	} else {
		/* Save current interrupt state */
		bool interrupt_enabled = is_interrupt_enabled();

		/*
		 * Updating timer shouldn't be interrupted (eg. when counter
		 * overflows) because it could lead to some unintended
		 * consequences. Please note that this function can be called
		 * with disabled or enabled interrupts so we need to restore
		 * the original state later.
		 */
		interrupt_disable();

		clksrc_high = ts.le.hi;
		__hw_clock_source_set(ts.le.lo);

		/* Restore original interrupt state */
		if (interrupt_enabled)
			interrupt_enable();
	}

	/* some timers might be already expired : process them */
	task_trigger_irq(timer_irq);
}

/*
 * Define versions of __hw_clock_source_read and __hw_clock_source_set
 * that wrap the 64-bit versions for chips with CONFIG_HWTIMER_64BIT.
 */
#ifdef CONFIG_HWTIMER_64BIT
__overridable uint32_t __hw_clock_source_read(void)
{
	return (uint32_t)__hw_clock_source_read64();
}

void __hw_clock_source_set(uint32_t ts)
{
	uint64_t current = __hw_clock_source_read64();

	__hw_clock_source_set64(((current >> 32) << 32) | ts);
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

void timer_init(void)
{
	const timestamp_t *ts;
	int size, version;

	/* Restore time from before sysjump */
	ts = (const timestamp_t *)system_get_jump_tag(TIMER_SYSJUMP_TAG,
						      &version, &size);
	if (ts && version == 1 && size == sizeof(timestamp_t)) {
		if (IS_ENABLED(CONFIG_HWTIMER_64BIT)) {
			timer_irq = __hw_clock_source_init64(ts->val);
		} else {
			clksrc_high = ts->le.hi;
			timer_irq = __hw_clock_source_init(ts->le.lo);
		}
	} else {
		if (IS_ENABLED(CONFIG_HWTIMER_64BIT))
			timer_irq = __hw_clock_source_init64(0);
		else
			timer_irq = __hw_clock_source_init(0);
	}
}

/* Preserve time across a sysjump */
static void timer_sysjump(void)
{
	timestamp_t ts = get_time();

	system_add_jump_tag(TIMER_SYSJUMP_TAG, 1, sizeof(ts), &ts);
}
DECLARE_HOOK(HOOK_SYSJUMP, timer_sysjump, HOOK_PRIO_DEFAULT);

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

#ifdef CONFIG_CMD_FORCETIME
/*
 * Force the hwtimer to a given time. This may have undesired consequences,
 * especially when going "backward" in time, because task deadlines are
 * left un-adjusted.
 */
static int command_force_time(int argc, const char **argv)
{
	char *e;
	timestamp_t new;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	new.le.hi = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	new.le.lo = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	ccprintf("Time: 0x%016llx = %lld.%06lld s\n", new.val,
		 new.val / USEC_PER_SEC, new.val % USEC_PER_SEC);
	force_time(new);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(forcetime, command_force_time, "hi lo",
			"Force current time");
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
