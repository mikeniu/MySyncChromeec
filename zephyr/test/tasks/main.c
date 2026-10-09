/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_tasks.h"
#include "task.h"
#include "timer.h"

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>
#include <zephyr/ztest.h>

/* Second for platform/ec task API (in microseconds). */
#define TASK_SEC(s) (s * 1000 * 1000)

K_SEM_DEFINE(task_done1, 0, 1);
K_SEM_DEFINE(task_done2, 0, 1);
K_SEM_DEFINE(test_ready1, 0, 1);
K_SEM_DEFINE(test_ready2, 0, 1);

static void (*task1)(void);
static void (*task2)(void);

static void run_test(void (*task1_run)(void), void (*task2_run)(void))
{
	task1 = task1_run;
	task2 = task2_run;
	k_sem_give(&test_ready1);
	k_sem_give(&test_ready2);
	k_sem_take(&task_done1, K_FOREVER);
	k_sem_take(&task_done2, K_FOREVER);
}

void task1_entry(void *p)
{
	while (1) {
		k_sem_take(&test_ready1, K_FOREVER);
		task1();
		k_sem_give(&task_done1);
	}
}

void task2_entry(void *p)
{
	while (1) {
		k_sem_take(&test_ready2, K_FOREVER);
		task2();
		k_sem_give(&task_done2);
	}
}

/*
 * Unlike Tasks 1 & 2, it is allowed to run Task 3 more than once per
 * call to run_test().  It will call task3_entry_func if set, and wait
 * for the next event.  This is useful to test things like timers,
 * which you are expecting the event to fire at some point in the
 * future, and you want to test that it happens.
 */
static void (*task3_entry_func)(uint32_t event_mask);

void task3_entry(void *p)
{
	uint32_t events = 0;

	for (;;) {
		if (task3_entry_func)
			task3_entry_func(events);
		events = task_wait_event(-1);
	}
}

static void set_event_before_task_start1(void)
{
	const uint32_t events = task_wait_event(TASK_SEC(2));

	zassert_equal(events, 0xAAAA, "Should have 0xAAAA events");
}

static void set_event_before_task_start2(void)
{
	/* Do nothing */
}

static void *tasks_setup(void)
{
	start_ec_tasks();

	return NULL;
}

ZTEST(test_task_shim, test_set_event_before_task_start)
{
	/* Send event before tasks start */
	task_set_event(TASK_ID_TASK_1, 0xAAAA);

	run_test(set_event_before_task_start1, set_event_before_task_start2);
}

static void task_get_current1(void)
{
	zassert_equal(task_get_current(), TASK_ID_TASK_1, "ID matches");
}

static void task_get_current2(void)
{
	zassert_equal(task_get_current(), TASK_ID_TASK_2, "ID matches");
}

ZTEST(test_task_shim, test_task_get_current)
{
	run_test(&task_get_current1, &task_get_current2);
}

static void timeout1(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event(TASK_SEC(2));
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, TASK_EVENT_TIMER, "Should have timeout event");
	zassert_within(end_ms - start_ms, 2000, 100, "Timeout for 2 seconds");
}

static void timeout2(void)
{
	/* Do nothing */
}

ZTEST(test_task_shim, test_timeout)
{
	run_test(&timeout1, &timeout2);
}

static void event_delivered1(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event(-1);
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, 0x1234, "Verify event bits");
	zassert_within(end_ms - start_ms, 5000, 100, "Waited for 5 seconds");
}

static void event_delivered2(void)
{
	k_sleep(K_SECONDS(5));

	task_set_event(TASK_ID_TASK_1, 0x1234);
}

ZTEST(test_task_shim, test_event_delivered)
{
	run_test(&event_delivered1, &event_delivered2);
}

static void event_mask_not_delivered1(void)
{
	task_set_event(TASK_ID_TASK_2, 0x007F);
}

static void event_mask_not_delivered2(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event_mask(0x0080, TASK_SEC(7));
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, TASK_EVENT_TIMER, "Should have timeout event");
	zassert_within(end_ms - start_ms, 7000, 100, "Timeout for 7 seconds");

	const uint32_t leftover_events = task_wait_event(0);

	zassert_equal(leftover_events, 0x007F, "All events should be waiting");
}

ZTEST(test_task_shim, test_event_mask_not_delivered)
{
	run_test(&event_mask_not_delivered1, &event_mask_not_delivered2);
}

static void event_mask_extra1(void)
{
	k_sleep(K_SECONDS(1));

	task_set_event(TASK_ID_TASK_2, 0x00FF);
}

static void event_mask_extra2(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event_mask(0x0001, TASK_SEC(10));
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, 0x0001, "Verify only waited for event");
	zassert_within(end_ms - start_ms, 1000, 100, "Timeout for 1 second");

	const uint32_t leftover_events = task_wait_event(0);

	zassert_equal(leftover_events, 0x00FE, "All events should be waiting");
}

ZTEST(test_task_shim, test_event_mask_extra)
{
	run_test(&event_mask_extra1, &event_mask_extra2);
}

static void empty_set_mask1(void)
{
	k_sleep(K_SECONDS(1));
	/*
	 * It is generally invalid to set a 0 event, but this simulates a race
	 * condition and exercises fallback code in task_wait_event
	 */
	task_set_event(TASK_ID_TASK_2, 0);
	k_sleep(K_SECONDS(1));
	task_set_event(TASK_ID_TASK_2, 0x1234);
}

static void empty_set_mask2(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events = task_wait_event_mask(0x1234, TASK_SEC(10));
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, 0x1234, "Verify only waited for event");
	zassert_within(end_ms - start_ms, 2000, 100, "Timeout for 2 seconds");
}

ZTEST(test_task_shim, test_empty_set_mask)
{
	run_test(&empty_set_mask1, &empty_set_mask2);
}

static bool pre_kernel_ran = false;
static int pre_kernel(void)
{
	zassert_false(pre_kernel_ran);
	zassert_true(k_is_pre_kernel());
	zassert_equal(TASK_ID_INVALID, task_get_current());
	zassert_false(in_deferred_context());
	pre_kernel_ran = true;
	return 0;
}
SYS_INIT(pre_kernel, PRE_KERNEL_2, 0);

ZTEST(test_task_shim, test_task_get_current_pre_kernel)
{
	zassert_true(pre_kernel_ran);
}

static bool post_kernel_ran = false;
static int post_kernel(void)
{
	zassert_false(post_kernel_ran);
	zassert_false(k_is_pre_kernel());
	zassert_equal(TASK_ID_MAIN, task_get_current());
	post_kernel_ran = true;
	return 0;
}
SYS_INIT(post_kernel, POST_KERNEL, 0);

ZTEST(test_task_shim, test_task_get_current_post_kernel)
{
	zassert_true(post_kernel_ran);
}

ZTEST_SUITE(test_task_shim, NULL, tasks_setup, NULL, NULL, NULL);
