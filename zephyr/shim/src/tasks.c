/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_interface.h"
#include "common.h"
#include "ec_tasks.h"
#include "host_command.h"
#include "task.h"
#include "zephyr_console_shim.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(task_shim, LOG_LEVEL_INF);

#define SAFE_THREAD_NAME(tid)                                           \
	((tid) && k_thread_name_get((tid)) ? k_thread_name_get((tid)) : \
					     "(unknown)")

/* Ensure that the idle task is at lower priority than lowest priority task. */
BUILD_ASSERT(EC_TASK_PRIORITY(EC_TASK_PRIO_LOWEST) < K_IDLE_PRIO,
	     "CONFIG_NUM_PREEMPT_PRIORITIES too small, some tasks would run at "
	     "idle priority");

/* Forward declare all task entry point functions */
#define CROS_EC_TASK(name, entry, ...) void entry(void *p);
CROS_EC_TASK_LIST
#undef CROS_EC_TASK

/* Statically declare all threads here */
#define CROS_EC_TASK(name, entry, parameter, stack_size, priority, options) \
	K_THREAD_DEFINE(name, stack_size, entry, parameter, NULL, NULL,     \
			EC_TASK_PRIORITY(priority), options, SYS_FOREVER_MS);
CROS_EC_TASK_LIST
#undef CROS_EC_TASK

/*
 * Create a mapping from the cros-ec task ID to the Zephyr thread.
 */
#undef CROS_EC_TASK
#define CROS_EC_TASK(_name, _entry, _parameter, _size, _prio, _options) _name,
#ifdef TEST_BUILD
static k_tid_t task_to_k_tid[TASK_ID_COUNT] = {
#else
const static k_tid_t task_to_k_tid[TASK_ID_COUNT] = {
#endif
	CROS_EC_TASK_LIST
};

static struct k_event task_events[TASK_ID_COUNT + EXTRA_TASK_COUNT];
#undef CROS_EC_TASK

test_export_static struct k_event *task_get_event(task_id_t cros_task_id)
{
	if (cros_task_id >= ARRAY_SIZE(task_events)) {
		return NULL;
	}

	return &task_events[cros_task_id];
}

test_export_static k_tid_t get_idle_thread(void)
{
	extern struct k_thread z_idle_threads[];

	if (!IS_ENABLED(CONFIG_SMP)) {
		return &z_idle_threads[0];
	}
	__ASSERT(false, "%s does not support SMP", __func__);
	return NULL;
}

test_export_static k_tid_t get_sysworkq_thread(void)
{
	return k_sys_work_q.thread_id;
}

k_tid_t get_main_thread(void)
{
	/* Pointer to the main thread, defined in kernel/init.c */
	extern struct k_thread z_main_thread;

	return &z_main_thread;
}

test_mockable k_tid_t get_hostcmd_thread(void)
{
#if defined(CONFIG_TASK_HOSTCMD_THREAD_MAIN) || \
	(defined(CONFIG_EC_HOST_CMD) &&         \
	 !defined(CONFIG_EC_HOST_CMD_DEDICATED_THREAD))
	return get_main_thread();
#elif defined(CONFIG_EC_HOST_CMD_DEDICATED_THREAD)
	const struct ec_host_cmd *hc = ec_host_cmd_get_hc();
	return hc->thread;
#elif defined(CONFIG_HAS_HOSTCMD)
	return task_to_k_tid[TASK_ID_HOSTCMD];
#else
	__ASSERT(false, "HOSTCMD task is not enabled");
	return NULL;
#endif
}

static task_id_t thread_id_to_task_id(k_tid_t thread_id)
{
	if (thread_id == NULL) {
		__ASSERT(false, "Invalid thread_id");
		return TASK_ID_INVALID;
	}

	if (get_sysworkq_thread() == thread_id) {
		return TASK_ID_SYSWORKQ;
	}

#if defined(CONFIG_HAS_HOSTCMD) || \
	(!defined(CONFIG_SHIMMED_TASKS) && !defined(CONFIG_HAS_TEST_TASKS))
	if (get_hostcmd_thread() == thread_id) {
		return TASK_ID_HOSTCMD;
	}
#endif

#ifdef HAS_TASK_MAIN
	if (get_main_thread() == thread_id) {
		return TASK_ID_MAIN;
	}
#endif /* HAS_TASK_MAIN */

	if (get_idle_thread() == thread_id) {
		return TASK_ID_IDLE;
	}

	if (get_shell_thread() == thread_id) {
		return TASK_ID_SHELL;
	}

#ifdef CONFIG_AP_PWRSEQ
	if (get_ap_pwrseq_thread() == thread_id) {
		return TASK_ID_AP_PWRSEQ;
	}
#endif /* CONFIG_AP_PWRSEQ */

	for (size_t i = 0; i < TASK_ID_COUNT; ++i) {
		if (task_to_k_tid[i] == thread_id) {
			return i;
		}
	}

	if (thread_id->base.thread_state & _THREAD_DUMMY) {
		return TASK_ID_INVALID;
	}

	LOG_ERR("Failed to map thread to task: thread_id=%p, name=%s",
		thread_id, SAFE_THREAD_NAME(thread_id));

	return TASK_ID_INVALID;
}

task_id_t task_get_current(void)
{
	/* k_current_get() is not valid pre kernel */
	if (k_is_pre_kernel()) {
		return TASK_ID_INVALID;
	}

	return thread_id_to_task_id(k_current_get());
}

void task_set_event(task_id_t cros_task_id, uint32_t event)
{
	struct k_event *ev = task_get_event(cros_task_id);

	if (ev != NULL) {
		k_event_post(ev, event);
	}
}

uint32_t task_wait_event(int timeout_us)
{
	return task_wait_event_mask(UINT32_MAX, timeout_us);
}

uint32_t task_wait_event_mask(uint32_t event_mask, int timeout_us)
{
	const k_timeout_t timeout = (timeout_us == -1) ? K_FOREVER :
							 K_USEC(timeout_us);
	struct k_event *ev = task_get_event(task_get_current());
	uint32_t events;

	__ASSERT_NO_MSG(ev != NULL);

	events = k_event_wait_safe(ev, event_mask, false, timeout);
	return events ? events : TASK_EVENT_TIMER;
}
#ifdef TEST_BUILD
void set_test_runner_tid(void)
{
	task_to_k_tid[TASK_ID_TEST_RUNNER] = k_current_get();
}

#ifdef CONFIG_TASKS_SET_TEST_RUNNER_TID_RULE
#include <zephyr/ztest.h>
static void set_test_runner_tid_rule_before(const struct ztest_unit_test *test,
					    void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	set_test_runner_tid();
}

ZTEST_RULE(set_test_runner_tid, set_test_runner_tid_rule_before, NULL);
#endif /* CONFIG_TASKS_SET_TEST_RUNNER_TID_RULE */
#endif /* TEST_BUILD */

void start_ec_tasks(void)
{
	for (size_t i = 0; i < TASK_ID_COUNT; ++i) {
#ifdef TEST_BUILD
		/* The test runner thread is automatically started. */
		if (i == TASK_ID_TEST_RUNNER) {
			continue;
		}
#endif
		k_thread_start(task_to_k_tid[i]);
	}
}

/*
 * Initialize all of the kernel objects before application code starts.
 * This allows us to set events on tasks before they even start, e.g. in
 * INIT_HOOKS.
 */
static int init_events(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(task_events); ++i) {
		k_event_init(&task_events[i]);
	}

	return 0;
}
SYS_INIT(init_events, POST_KERNEL, 50);
