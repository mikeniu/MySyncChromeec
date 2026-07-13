/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "hooks.h"
#include "host_command_memory_dump.h"
#include "string.h"

#include <zephyr/arch/cpu.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>

#ifdef CONFIG_THREAD_MONITOR
#ifdef CONFIG_HAS_TASK_KEYSCAN
extern const k_tid_t KEYSCAN;
#endif
#ifdef CONFIG_HAS_TASK_KEYPROTO
extern const k_tid_t KEYPROTO;
#endif
#ifdef CONFIG_HAS_TASK_WOV
extern const k_tid_t WOV;
#endif

static bool thread_is_excluded_from_memory_dump(k_tid_t thread)
{
#ifdef CONFIG_HAS_TASK_KEYSCAN
	if (thread == KEYSCAN) {
		return true;
	}
#endif
#ifdef CONFIG_HAS_TASK_KEYPROTO
	if (thread == KEYPROTO) {
		return true;
	}
#endif
#ifdef CONFIG_HAS_TASK_WOV
	if (thread == WOV) {
		return true;
	}
#endif
	return false;
}

test_export_static void register_thread_memory_dump(k_tid_t thread)
{
	register_memory_dump(thread->stack_info.start,
			     thread->stack_info.size -
				     thread->stack_info.delta);
}

static void register_thread_cb(const struct k_thread *thread, void *user_data)
{
	ARG_UNUSED(user_data);
	k_tid_t tid = (k_tid_t)thread;

	if (thread_is_excluded_from_memory_dump(tid)) {
		return;
	}
	register_thread_memory_dump(tid);
}
#endif /* CONFIG_THREAD_MONITOR */

test_export_static void register_known_threads_memory_dump(void)
{
#ifdef CONFIG_THREAD_MONITOR
	k_thread_foreach(register_thread_cb, NULL);
#endif
}
DECLARE_HOOK(HOOK_INIT, register_known_threads_memory_dump, HOOK_PRIO_FIRST);
