/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "task.h"

#include <zephyr/toolchain.h>

#define CROS_EC_TASK(name, ...) __weak const k_tid_t name = NULL;
#define TASK_TEST(name, ...) CROS_EC_TASK(name)
CROS_EC_TASK_LIST
#undef CROS_EC_TASK
#undef TASK_TEST

#include <zephyr/fff.h>
#include <zephyr/sys/atomic.h>

/*
 * The fakes below are needed to satisfy the final link, but are never
 * actually called at runtime. Exclude from the coverage report to avoid
 * false positives on the low-coverage check.
 */
/* LCOV_EXCL_START */
FAKE_VALUE_FUNC(atomic_t *, task_get_event_bitmap, k_tid_t);
FAKE_VOID_FUNC(task_set_event, k_tid_t, uint32_t);
FAKE_VALUE_FUNC(int, task_start_called);
FAKE_VALUE_FUNC(uint32_t, task_wait_event, int);
FAKE_VALUE_FUNC(uint32_t, task_wait_event_mask, uint32_t, int);

/*
 * If cros-ec tasks aren't enabled, always indicate we are in a deferred
 * context.
 */
bool in_deferred_context(void)
{
	return true;
}

__weak const k_tid_t get_test_runner_tid(void)
{
	return NULL;
}

__weak k_tid_t pd_port_to_thread_id(int port)
{
	return NULL;
}

__weak int pd_port_from_thread(k_tid_t thread)
{
	return 0;
}
/* LCOV_EXCL_STOP */
