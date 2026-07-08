/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/tracing/tracing.h>
#include <string.h>

static bool sysworkq_in_trace;

void sys_trace_k_thread_sleep_enter_user(k_timeout_t timeout)
{
	struct k_thread *curr = k_current_get();
	const char *name = k_thread_name_get(curr);

	if (name && strcmp(name, "sysworkq") == 0) {
		if (sysworkq_in_trace) {
			return;
		}
		sysworkq_in_trace = true;
		printk("[TRACE] sysworkq sleep enter: %lld ticks (%lld ms), caller: %p\n",
		       (long long)timeout.ticks,
		       (long long)k_ticks_to_ms_floor64(timeout.ticks),
		       __builtin_return_address(0));
		sysworkq_in_trace = false;
	}
}

void sys_trace_k_thread_sleep_exit_user(k_timeout_t timeout, int32_t ret)
{
	struct k_thread *curr = k_current_get();
	const char *name = k_thread_name_get(curr);

	if (name && strcmp(name, "sysworkq") == 0) {
		if (sysworkq_in_trace) {
			return;
		}
		sysworkq_in_trace = true;
		printk("[TRACE] sysworkq sleep exit: ret %d\n", ret);
		sysworkq_in_trace = false;
	}
}

void sys_trace_k_thread_msleep_enter_user(int32_t ms)
{
	struct k_thread *curr = k_current_get();
	const char *name = k_thread_name_get(curr);

	if (name && strcmp(name, "sysworkq") == 0) {
		if (sysworkq_in_trace) {
			return;
		}
		sysworkq_in_trace = true;
		printk("[TRACE] sysworkq msleep enter: %d ms\n", ms);
		sysworkq_in_trace = false;
	}
}

void sys_trace_k_thread_msleep_exit_user(int32_t ms, int32_t ret)
{
	struct k_thread *curr = k_current_get();
	const char *name = k_thread_name_get(curr);

	if (name && strcmp(name, "sysworkq") == 0) {
		if (sysworkq_in_trace) {
			return;
		}
		sysworkq_in_trace = true;
		printk("[TRACE] sysworkq msleep exit: ret %d\n", ret);
		sysworkq_in_trace = false;
	}
}

void sys_trace_k_thread_usleep_enter_user(int32_t us)
{
	struct k_thread *curr = k_current_get();
	const char *name = k_thread_name_get(curr);

	if (name && strcmp(name, "sysworkq") == 0) {
		if (sysworkq_in_trace) {
			return;
		}
		sysworkq_in_trace = true;
		printk("[TRACE] sysworkq usleep enter: %d us, caller: %p\n",
		       us,
		       __builtin_return_address(0));
		sysworkq_in_trace = false;
	}
}

void sys_trace_k_thread_usleep_exit_user(int32_t us, int32_t ret)
{
	struct k_thread *curr = k_current_get();
	const char *name = k_thread_name_get(curr);

	if (name && strcmp(name, "sysworkq") == 0) {
		if (sysworkq_in_trace) {
			return;
		}
		sysworkq_in_trace = true;
		printk("[TRACE] sysworkq usleep exit: ret %d\n", ret);
		sysworkq_in_trace = false;
	}
}
