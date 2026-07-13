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

#undef HOSTCMD

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

#define CROS_EC_TASK(name, entry, parameter, stack_size, priority, options) \
	K_THREAD_DEFINE(name, stack_size, entry, parameter, NULL, NULL,     \
			EC_TASK_PRIORITY(priority), options, SYS_FOREVER_MS);
#define TASK_TEST(name, e, p, size) CROS_EC_TASK(name, e, p, size)
CROS_EC_TASK_LIST
#undef CROS_EC_TASK
#undef TASK_TEST

struct task_ctx_base_data {
	/** A wait-able event that is raised when a new task event is posted */
	struct k_poll_signal new_event;
	/** The current platform/ec events set for this task/thread */
	atomic_t event_mask;
	/** Task timer structure */
	struct k_timer timer;
};

#define CROS_EC_TASK(name, ...) +1
#define TASK_TEST(name, ...) CROS_EC_TASK(name)
enum { SHIMMED_TASK_COUNT = 0 CROS_EC_TASK_LIST };
#undef CROS_EC_TASK
#undef TASK_TEST

#define EXTRA_TASK_IDX(name) EXTRA_TASK_IDX_##name,
enum { CROS_EC_EXTRA_TASKS(EXTRA_TASK_IDX) EXTRA_TASK_IDX_COUNT };
#undef EXTRA_TASK_IDX

static struct task_ctx_base_data shimmed_tasks_data[SHIMMED_TASK_COUNT];
static struct task_ctx_base_data extra_tasks_data[EXTRA_TASK_COUNT];

static const k_tid_t shimmed_task_tids[] = {
#define CROS_EC_TASK(name, ...) name,
#define TASK_TEST(name, ...) CROS_EC_TASK(name)
	CROS_EC_TASK_LIST
#undef CROS_EC_TASK
#undef TASK_TEST
		NULL
};

static int tasks_started;

test_export_static k_tid_t get_idle_thread(void);
test_export_static k_tid_t get_sysworkq_thread(void);

#define get_HOSTCMD_thread_local() get_hostcmd_thread()
#define get_MAIN_thread_local() get_main_thread()
#define get_SYSWORKQ_thread_local() get_sysworkq_thread()
#define get_IDLE_thread_local() get_idle_thread()
#define get_SHELL_thread_local() get_shell_thread()
#define get_AP_PWRSEQ_thread_local() get_ap_pwrseq_thread()

#ifdef TEST_BUILD
static struct task_ctx_base_data test_runner_data;
#endif

static struct task_ctx_base_data *find_extra_base_data(k_tid_t thread)
{
#define CHECK_EXTRA_TASK(name)                                   \
	if (get_##name##_thread_local() == thread) {             \
		return &extra_tasks_data[EXTRA_TASK_IDX_##name]; \
	}
	CROS_EC_EXTRA_TASKS(CHECK_EXTRA_TASK)
#undef CHECK_EXTRA_TASK
#ifdef TEST_BUILD
	if (get_test_runner_tid() && get_test_runner_tid() == thread) {
		return &test_runner_data;
	}
#endif
	return NULL;
}

static struct task_ctx_base_data *find_base_data_by_search(k_tid_t thread)
{
	for (size_t i = 0; i < SHIMMED_TASK_COUNT; ++i) {
		if (shimmed_task_tids[i] == thread) {
			return &shimmed_tasks_data[i];
		}
	}
	return find_extra_base_data(thread);
}

static inline struct task_ctx_base_data *task_get_base_data(k_tid_t thread)
{
	if (thread == NULL) {
		return NULL;
	}
	return find_base_data_by_search(thread);
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
#ifdef HAS_TASK_HOSTCMD
#ifdef CONFIG_TASK_HOSTCMD_THREAD_MAIN
	return get_main_thread();
#else
#ifndef CONFIG_EC_HOST_CMD
	return HOSTCMD;
#else
	const struct ec_host_cmd *hc = ec_host_cmd_get_hc();

	return (k_tid_t)&hc->thread;
#endif /* CONFIG_EC_HOST_CMD */
#endif /* CONFIG_TASK_HOSTCMD_THREAD_MAIN */
#endif /* HAS_TASK_HOSTCMD */
	__ASSERT(false, "HOSTCMD task is not enabled");
	return NULL;
}

atomic_t *task_get_event_bitmap(k_tid_t thread)
{
	struct task_ctx_base_data *data;

	data = task_get_base_data(thread);

	return data == NULL ? NULL : &data->event_mask;
}

void task_set_event(k_tid_t thread, uint32_t event)
{
	struct task_ctx_base_data *data;

	data = task_get_base_data(thread);

	if (data != NULL) {
		atomic_or(&data->event_mask, event);
		k_poll_signal_raise(&data->new_event, 0);
	}
}

uint32_t task_wait_event(int timeout_us)
{
	struct task_ctx_base_data *data = task_get_base_data(k_current_get());

	__ASSERT_NO_MSG(data != NULL);

	const k_timeout_t timeout = (timeout_us == -1) ? K_FOREVER :
							 K_USEC(timeout_us);
	const int64_t tick_deadline =
		k_uptime_ticks() + k_us_to_ticks_near64(timeout_us);

	struct k_poll_event poll_events[1] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
					 K_POLL_MODE_NOTIFY_ONLY,
					 &data->new_event),
	};

	/* Wait for signal, then clear it before reading events */
	const int rv = k_poll(poll_events, ARRAY_SIZE(poll_events), timeout);

	k_poll_signal_reset(&data->new_event);
	uint32_t events = atomic_set(&data->event_mask, 0);

	if (rv == -EAGAIN) {
		events |= TASK_EVENT_TIMER;
	}

	/* If we didn't get an event, we need to wait again. There is a very
	 * small chance of us reading the event_mask one signaled event too
	 * early. In that case, just wait again for the remaining timeout
	 */
	if (events == 0) {
		const int64_t ticks_left = tick_deadline - k_uptime_ticks();

		events |= TASK_EVENT_TIMER;

		if (ticks_left > 0) {
			return task_wait_event(
				k_ticks_to_us_near64(ticks_left));
		}
	}

	return events;
}

uint32_t task_wait_event_mask(uint32_t event_mask, int timeout_us)
{
	struct task_ctx_base_data *data = task_get_base_data(k_current_get());
	__ASSERT_NO_MSG(data != NULL);

	uint32_t events = 0;
	const int64_t tick_deadline =
		k_uptime_ticks() + k_us_to_ticks_near64(timeout_us);

	/*  Need to return timeout flags if it occurs as well */
	event_mask |= TASK_EVENT_TIMER;

	while (!(event_mask & events)) {
		const int64_t ticks_left = tick_deadline - k_uptime_ticks();

		if (timeout_us != -1 && ticks_left <= 0) {
			events |= TASK_EVENT_TIMER;
			break;
		}

		struct k_poll_event poll_events[1] = {
			K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
						 K_POLL_MODE_NOTIFY_ONLY,
						 &data->new_event),
		};

		/* Ensure to honor the -1 timeout as FOREVER */
		k_poll(poll_events, ARRAY_SIZE(poll_events),
		       timeout_us == -1 ? K_FOREVER : K_TICKS(ticks_left));
		k_poll_signal_reset(&data->new_event);
		events |= atomic_set(&data->event_mask, 0);
	}

	/* Replace any events that weren't in the mask */
	if (events & ~event_mask) {
		atomic_or(&data->event_mask, events & ~event_mask);
		k_poll_signal_raise(&data->new_event, 0);
	}

	return events & event_mask;
}

/*
 * Callback function to use with k_timer_start to set the
 * TASK_EVENT_TIMER event on a task.
 */
static void timer_expire(struct k_timer *timer_id)
{
	struct task_ctx_base_data *data =
		CONTAINER_OF(timer_id, struct task_ctx_base_data, timer);

	atomic_or(&data->event_mask, TASK_EVENT_TIMER);
	k_poll_signal_raise(&data->new_event, 0);
}

int timer_arm(timestamp_t event, k_tid_t thread)
{
	struct task_ctx_base_data *data = task_get_base_data(thread);
	timestamp_t now = get_time();

	if (!data)
		return EC_ERROR_INVAL;

	if (event.val <= now.val) {
		/* Timer requested for now or in the past, fire right away */
		atomic_or(&data->event_mask, TASK_EVENT_TIMER);
		k_poll_signal_raise(&data->new_event, 0);
		return EC_SUCCESS;
	}

	/* Check for a running timer */
	if (k_timer_remaining_get(&data->timer))
		return EC_ERROR_BUSY;

	k_timer_start(&data->timer, K_USEC(event.val - now.val), K_NO_WAIT);
	return EC_SUCCESS;
}

void timer_cancel(k_tid_t thread)
{
	struct task_ctx_base_data *data = task_get_base_data(thread);

	if (data) {
		k_timer_stop(&data->timer);
	}
}

#ifdef TEST_BUILD
static k_tid_t test_runner_tid;

void set_test_runner_tid(void)
{
	k_tid_t current = k_current_get();

	k_poll_signal_init(&test_runner_data.new_event);
	k_timer_init(&test_runner_data.timer, timer_expire, NULL);
	test_runner_tid = current;
}

const k_tid_t get_test_runner_tid(void)
{
	return test_runner_tid;
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
#define CROS_EC_TASK(name, ...) k_thread_start(name);
#define TASK_TEST(name, ...) CROS_EC_TASK(name)
	CROS_EC_TASK_LIST
#undef CROS_EC_TASK
#undef TASK_TEST

	tasks_started = 1;
}

/*
 * Initialize all of the kernel objects before application code starts.
 * This allows us to set events on tasks before they even start, e.g. in
 * INIT_HOOKS.
 */
int init_signals(void)
{
	for (size_t i = 0; i < SHIMMED_TASK_COUNT; ++i) {
		k_poll_signal_init(&shimmed_tasks_data[i].new_event);
		k_timer_init(&shimmed_tasks_data[i].timer, timer_expire, NULL);
	}

	for (size_t i = 0; i < EXTRA_TASK_COUNT; ++i) {
		k_poll_signal_init(&extra_tasks_data[i].new_event);
		k_timer_init(&extra_tasks_data[i].timer, timer_expire, NULL);
	}

	return 0;
}
SYS_INIT(init_signals, POST_KERNEL, 50);

int task_start_called(void)
{
	return tasks_started;
}

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

#ifdef CONFIG_USB_PD_PORT_MAX_COUNT
k_tid_t pd_port_to_thread_id(int port)
{
	switch (port) {
#ifdef HAS_TASK_PD_C0
	case 0:
		return PD_C0;
#endif
#ifdef HAS_TASK_PD_C1
	case 1:
		return PD_C1;
#endif
#ifdef HAS_TASK_PD_C2
	case 2:
		return PD_C2;
#endif
#ifdef HAS_TASK_PD_C3
	case 3:
		return PD_C3;
#endif
	default:
		return NULL;
	}
}

int pd_port_from_thread(k_tid_t thread)
{
#ifdef HAS_TASK_PD_C0
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (pd_port_to_thread_id(i) == thread) {
			return i;
		}
	}
#endif
	return -1;
}
#endif /* CONFIG_USB_PD_PORT_MAX_COUNT */
