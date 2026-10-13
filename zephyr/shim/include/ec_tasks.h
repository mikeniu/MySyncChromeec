/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_EC_TASKS_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_EC_TASKS_H_

#include "task.h"

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Starts all of the shimmed EC tasks. Requires CONFIG_SHIMMED_TASKS=y. */
void start_ec_tasks(void);

struct task_ctx_base_data {
	/** A wait-able event that is raised when a new task event is posted */
	struct k_poll_signal new_event;
	/** The current platform/ec events set for this task/thread */
	atomic_t event_mask;
};

#ifdef TEST_BUILD
/**
 * Set TASK_ID_TEST_RUNNER to current thread tid. Some functions that are tested
 * require to run in any task context.
 */
void set_test_runner_tid(void);

struct task_ctx_base_data *task_get_base_data(task_id_t cros_task_id);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_EC_TASKS_H_ */
