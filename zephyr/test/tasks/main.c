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
	for (;;) {
		k_sleep(K_FOREVER);
	}
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

K_EVENT_DEFINE(test_domain_event);

static void test_wait_safe1(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events =
		cros_ec_event_wait_safe(&test_domain_event, 0x1234, -1);
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, 0x1234, "Verify event bits");
	zassert_within(end_ms - start_ms, 100, 50, "Waited for event");
}

static void test_wait_safe2(void)
{
	k_sleep(K_MSEC(100));
	k_event_post(&test_domain_event, 0x1234);
}

ZTEST(test_task_shim, test_cros_ec_event_wait_safe)
{
	run_test(&test_wait_safe1, &test_wait_safe2);
}

static void test_timeout1(void)
{
	const uint32_t start_ms = k_uptime_get();
	const uint32_t events =
		cros_ec_event_wait_safe(&test_domain_event, 0x1, 500000);
	const uint32_t end_ms = k_uptime_get();

	zassert_equal(events, TASK_EVENT_TIMER,
		      "Should return TIMER on timeout");
	zassert_within(end_ms - start_ms, 500, 100, "Timeout after 500ms");
}

static void test_timeout2(void)
{
	/* Do nothing */
}

ZTEST(test_task_shim, test_cros_ec_event_wait_safe_timeout)
{
	run_test(&test_timeout1, &test_timeout2);
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

static void *tasks_setup(void)
{
	start_ec_tasks();

	return NULL;
}

ZTEST(test_task_shim, test_task_get_current_post_kernel)
{
	zassert_true(post_kernel_ran);
}

ZTEST_SUITE(test_task_shim, NULL, tasks_setup, NULL, NULL, NULL);
