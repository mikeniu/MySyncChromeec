/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_interface.h"
#include "common.h"
#include "ec_tasks.h"
#include "host_command.h"
#include "task.h"
#include "timer.h"
#include "usb_pd.h"
#include "zephyr_console_shim.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(task_shim, LOG_LEVEL_INF);

#include <soc.h>

#define SAFE_THREAD_NAME(tid)                                           \
	((tid) && k_thread_name_get((tid)) ? k_thread_name_get((tid)) : \
					     "(unknown)")

/* Ensure that the idle task is at lower priority than lowest priority task. */
BUILD_ASSERT(EC_TASK_PRIORITY(EC_TASK_PRIO_LOWEST) < K_IDLE_PRIO,
	     "CONFIG_NUM_PREEMPT_PRIORITIES too small, some tasks would run at "
	     "idle priority");

/* Forward declare all task entry point functions */
#define CROS_EC_TASK(name, entry, ...) void entry(void *p);
#define TASK_TEST(name, entry, ...) CROS_EC_TASK(name, entry)
CROS_EC_TASK_LIST
#undef CROS_EC_TASK
#undef TASK_TEST

/* Statically declare all threads here */
#define CROS_EC_TASK(name, entry, parameter, stack_size, priority, options) \
	K_THREAD_DEFINE(name, stack_size, entry, parameter, NULL, NULL,     \
			EC_TASK_PRIORITY(priority), options, SYS_FOREVER_MS);
#define TASK_TEST(name, e, p, size) CROS_EC_TASK(name, e, p, size)
CROS_EC_TASK_LIST
#undef CROS_EC_TASK
#undef TASK_TEST

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

/* Task timer structures. Keep separate from the context ones to avoid memory
 * holes due to int64_t fields in struct _timeout.
 */
static struct k_timer shimmed_tasks_timers[TASK_ID_COUNT + EXTRA_TASK_COUNT];

static int tasks_started;
#undef CROS_EC_TASK
#undef TASK_TEST

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
#ifdef HAS_TASK_HOSTCMD
#ifdef CONFIG_TASK_HOSTCMD_THREAD_MAIN
	return get_main_thread();
#else
#ifndef CONFIG_EC_HOST_CMD
	return task_to_k_tid[TASK_ID_HOSTCMD];
#else
	const struct ec_host_cmd *hc = ec_host_cmd_get_hc();

	return (k_tid_t)&hc->thread;
#endif /* CONFIG_EC_HOST_CMD */
#endif /* CONFIG_TASK_HOSTCMD_THREAD_MAIN */
#endif /* HAS_TASK_HOSTCMD */
	__ASSERT(false, "HOSTCMD task is not enabled");
	return NULL;
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

#ifdef HAS_TASK_HOSTCMD
	if (get_hostcmd_thread() == thread_id) {
		return TASK_ID_HOSTCMD;
	}
#endif /* HAS_TASK_HOSTCMD */

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

__noinline uint32_t cros_ec_event_wait_safe(struct k_event *event,
					    uint32_t events, int timeout_us)
{
	k_timeout_t timeout = (timeout_us == -1) ? K_FOREVER :
						   K_USEC(timeout_us);
	uint32_t evt = k_event_wait_safe(event, events, false, timeout);
	if (evt == 0 && timeout_us != -1) {
		evt = TASK_EVENT_TIMER;
	}
	return evt;
}

/* Stub for legacy timer expiration until timer_arm is refactored */
static void timer_expire(struct k_timer *timer_id)
{
	task_id_t cros_ec_task_id = timer_id - shimmed_tasks_timers;
#if defined(HAS_TASK_PD_C0) && defined(CONFIG_USB_PD_PORT_MAX_COUNT)
	if (cros_ec_task_id >= TASK_ID_PD_C0 &&
	    cros_ec_task_id < TASK_ID_PD_C0 + CONFIG_USB_PD_PORT_MAX_COUNT) {
		pd_set_event(TASK_ID_TO_PD_PORT(cros_ec_task_id),
			     TASK_EVENT_TIMER);
	}
#endif
}

int timer_arm(timestamp_t event, task_id_t cros_ec_task_id)
{
	struct k_timer *timer;
	timestamp_t now = get_time();

	timer = &shimmed_tasks_timers[cros_ec_task_id];

	if (event.val <= now.val) {
		/* Timer requested for now or in the past, fire right away */
		timer_expire(timer);
		return EC_SUCCESS;
	}

	/* Check for a running timer */
	if (k_timer_remaining_get(timer))
		return EC_ERROR_BUSY;

	k_timer_start(timer, K_USEC(event.val - now.val), K_NO_WAIT);
	return EC_SUCCESS;
}

void timer_cancel(task_id_t cros_ec_task_id)
{
	struct k_timer *timer;

	timer = &shimmed_tasks_timers[cros_ec_task_id];

	k_timer_stop(timer);
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
	for (size_t i = 0; i < TASK_ID_COUNT + EXTRA_TASK_COUNT; ++i) {
		k_timer_init(&shimmed_tasks_timers[i], timer_expire, NULL);
	}

	for (size_t i = 0; i < TASK_ID_COUNT; ++i) {
#ifdef TEST_BUILD
		/* The test runner thread is automatically started. */
		if (i == TASK_ID_TEST_RUNNER) {
			continue;
		}
#endif
		k_thread_start(task_to_k_tid[i]);
	}

	tasks_started = 1;
}

int task_start_called(void)
{
	return tasks_started;
}
/*
 * TODO(b/190203712): Implement this
 * LCOV_EXCL_START
 */
void task_disable_task(task_id_t tskid)
{
}
/* LCOV_EXCL_STOP */

/*
 * This function cannot be tested since it is architecture specific.
 * LCOV_EXCL_START
 */
void task_clear_pending_irq(int irq)
{
#if CONFIG_HAS_ITE_INTC
	ite_intc_isr_clear(irq);
#endif
}
/* LCOV_EXCL_STOP */

void task_enable_irq(int irq)
{
	arch_irq_enable(irq);
}

void task_disable_irq(int irq)
{
	arch_irq_disable(irq);
}

inline bool in_interrupt_context(void)
{
	return k_is_in_isr();
}

inline bool in_deferred_context(void)
{
	/* k_current_get() is not valid pre kernel */
	if (k_is_pre_kernel()) {
		return false;
	}
	/*
	 * Deferred calls run in the sysworkq.
	 */
	return (k_current_get() == get_sysworkq_thread());
}
