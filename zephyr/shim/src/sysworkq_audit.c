/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/tracing/tracing.h>

extern struct k_work_q k_sys_work_q;

#if defined(CONFIG_TRACING_USER)

static inline bool is_sysworkq_thread(void)
{
	k_tid_t sysworkq_tid = k_work_queue_thread_get(&k_sys_work_q);

	return (sysworkq_tid != NULL) && (k_current_get() == sysworkq_tid);
}

#if defined(CONFIG_PLATFORM_EC_SYSWORKQ_PANIC_ON_VIOLATION)
#define REPORT_VIOLATION(fmt, ...)                                         \
	do {                                                               \
		printk("\n[SYSWORKQ VIOLATION] " fmt "\n", ##__VA_ARGS__); \
		__ASSERT(0, fmt, ##__VA_ARGS__);                           \
	} while (0)
#else
#define REPORT_VIOLATION(fmt, ...)                                         \
	do {                                                               \
		printk("\n[SYSWORKQ VIOLATION] " fmt "\n", ##__VA_ARGS__); \
	} while (0)
#endif

__attribute__((used)) void
sys_trace_k_thread_sleep_ticks_enter_user(k_timeout_t timeout)
{
	if (is_sysworkq_thread()) {
		REPORT_VIOLATION("sleep called from %p (ticks %lld)!",
				 __builtin_return_address(0),
				 (long long)timeout.ticks);
	}
}

__attribute__((used)) void
sys_trace_k_mutex_lock_enter_user(struct k_mutex *mutex, k_timeout_t timeout)
{
	if (is_sysworkq_thread()) {
		if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			REPORT_VIOLATION(
				"blocking mutex_lock from %p (mutex %p, ticks %lld)!",
				__builtin_return_address(0), mutex,
				(long long)timeout.ticks);
		}
	}
}

__attribute__((used)) void sys_trace_k_sem_take_enter_user(struct k_sem *sem,
							   k_timeout_t timeout)
{
	if (is_sysworkq_thread()) {
		if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			REPORT_VIOLATION(
				"blocking sem_take from %p (sem %p, ticks %lld)!",
				__builtin_return_address(0), sem,
				(long long)timeout.ticks);
		}
	}
}

__attribute__((used)) void
sys_trace_k_condvar_wait_enter_user(struct k_condvar *condvar,
				    k_timeout_t timeout)
{
	if (is_sysworkq_thread()) {
		REPORT_VIOLATION("condvar wait called from %p!",
				 __builtin_return_address(0));
	}
}

__attribute__((used)) void
sys_trace_k_event_wait_enter_user(struct k_event *event, uint32_t events,
				  uint32_t options, k_timeout_t timeout)
{
	if (is_sysworkq_thread()) {
		if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			REPORT_VIOLATION(
				"blocking event_wait called from %p (ticks %lld)!",
				__builtin_return_address(0),
				(long long)timeout.ticks);
		}
	}
}

__attribute__((used)) void sys_trace_k_msgq_get_enter_user(struct k_msgq *msgq,
							   k_timeout_t timeout)
{
	if (is_sysworkq_thread()) {
		if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			REPORT_VIOLATION(
				"blocking msgq_get called from %p (ticks %lld)!",
				__builtin_return_address(0),
				(long long)timeout.ticks);
		}
	}
}

__attribute__((used)) void sys_trace_k_msgq_put_enter_user(struct k_msgq *msgq,
							   k_timeout_t timeout)
{
	if (is_sysworkq_thread()) {
		if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			REPORT_VIOLATION(
				"blocking msgq_put called from %p (ticks %lld)!",
				__builtin_return_address(0),
				(long long)timeout.ticks);
		}
	}
}

__attribute__((used)) void
sys_trace_k_thread_join_enter_user(struct k_thread *thread, k_timeout_t timeout)
{
	if (is_sysworkq_thread()) {
		REPORT_VIOLATION("thread join called from %p!",
				 __builtin_return_address(0));
	}
}

__attribute__((used)) void
sys_trace_k_work_cancel_sync_blocking_user(struct k_work *work,
					   struct k_work_sync *sync)
{
	if (is_sysworkq_thread()) {
		REPORT_VIOLATION("blocking work_cancel_sync from %p (work %p)!",
				 __builtin_return_address(0), work);
	}
}

__attribute__((used)) void
sys_trace_k_work_flush_blocking_user(struct k_work *work, k_timeout_t timeout)
{
	if (is_sysworkq_thread()) {
		if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			REPORT_VIOLATION(
				"blocking work_flush from %p (work %p, ticks %lld)!",
				__builtin_return_address(0), work,
				(long long)timeout.ticks);
		}
	}
}

#endif /* CONFIG_TRACING_USER */
