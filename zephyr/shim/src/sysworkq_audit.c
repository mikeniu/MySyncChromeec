/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/tracing/tracing.h>

extern struct k_work_q k_sys_work_q;

static atomic_t sysworkq_violation_count;
static bool in_violation_report;

uint32_t sysworkq_audit_get_violation_count(void)
{
	return (uint32_t)atomic_get(&sysworkq_violation_count);
}

void sysworkq_audit_reset_violation_count(void)
{
	atomic_clear(&sysworkq_violation_count);
}

static bool should_audit_sysworkq(void)
{
	return !in_violation_report &&
	       (k_current_get() == k_work_queue_thread_get(&k_sys_work_q));
}

static void report_violation(const char *op, const void *caller,
			     const void *obj, int64_t ticks)
{
	in_violation_report = true;
	atomic_inc(&sysworkq_violation_count);
	printk("\n[SYSWORKQ VIOLATION] %s from %p (obj %p, ticks %lld)!\n", op,
	       caller, obj, (long long)ticks);
#if defined(CONFIG_PLATFORM_EC_SYSWORKQ_PANIC_ON_VIOLATION)
	/* LCOV_EXCL_START */
	__ASSERT(0, "%s from %p (obj %p, ticks %lld)!", op, caller, obj,
		 (long long)ticks);
	/* LCOV_EXCL_STOP */
#endif
	in_violation_report = false;
}

static void check_non_blocking_op(const char *op, const void *caller,
				  const void *obj, int64_t ticks)
{
	if (should_audit_sysworkq()) {
		report_violation(op, caller, obj, ticks);
	}
}

void sysworkq_audit_check_non_blocking(const char *func, const void *caller)
{
	check_non_blocking_op(func, caller, NULL, 0);
}

#if defined(CONFIG_TRACING_USER)

static void check_blocking_timeout(const char *op, const void *caller,
				   const void *obj, k_timeout_t timeout)
{
	if (should_audit_sysworkq() && !K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		report_violation(op, caller, obj, timeout.ticks);
	}
}

__attribute__((used)) void
sys_trace_k_thread_sleep_ticks_enter_user(k_timeout_t timeout)
{
	check_non_blocking_op("sleep", __builtin_return_address(0), NULL,
			      timeout.ticks);
}

__attribute__((used)) void
sys_trace_k_mutex_lock_enter_user(struct k_mutex *mutex, k_timeout_t timeout)
{
	check_blocking_timeout("blocking mutex_lock",
			       __builtin_return_address(0), mutex, timeout);
}

__attribute__((used)) void sys_trace_k_sem_take_enter_user(struct k_sem *sem,
							   k_timeout_t timeout)
{
	check_blocking_timeout("blocking sem_take", __builtin_return_address(0),
			       sem, timeout);
}

__attribute__((used)) void
sys_trace_k_condvar_wait_enter_user(struct k_condvar *condvar,
				    k_timeout_t timeout)
{
	check_blocking_timeout("blocking condvar_wait",
			       __builtin_return_address(0), condvar, timeout);
}

__attribute__((used)) void
sys_trace_k_event_wait_enter_user(struct k_event *event, uint32_t events,
				  uint32_t options, k_timeout_t timeout)
{
	ARG_UNUSED(events);
	ARG_UNUSED(options);

	check_blocking_timeout("blocking event_wait",
			       __builtin_return_address(0), event, timeout);
}

__attribute__((used)) void sys_trace_k_msgq_get_enter_user(struct k_msgq *msgq,
							   k_timeout_t timeout)
{
	check_blocking_timeout("blocking msgq_get", __builtin_return_address(0),
			       msgq, timeout);
}

__attribute__((used)) void sys_trace_k_msgq_put_enter_user(struct k_msgq *msgq,
							   k_timeout_t timeout)
{
	check_blocking_timeout("blocking msgq_put", __builtin_return_address(0),
			       msgq, timeout);
}

__attribute__((used)) void
sys_trace_k_thread_join_enter_user(struct k_thread *thread, k_timeout_t timeout)
{
	check_blocking_timeout("blocking thread_join",
			       __builtin_return_address(0), thread, timeout);
}

__attribute__((used)) void
sys_trace_k_work_cancel_sync_enter_user(struct k_work *work,
					struct k_work_sync *sync)
{
	ARG_UNUSED(sync);

	check_non_blocking_op("blocking work_cancel_sync",
			      __builtin_return_address(0), work, 0);
}

__attribute__((used)) void
sys_trace_k_work_flush_enter_user(struct k_work *work)
{
	check_non_blocking_op("blocking work_flush",
			      __builtin_return_address(0), work, 0);
}

#endif /* CONFIG_TRACING_USER */
