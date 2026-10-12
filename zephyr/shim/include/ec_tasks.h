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

/**
 * Get the motion sense thread.
 *
 * @return Thread ID, or NULL if there is no motion sense thread.
 */
k_tid_t get_motionsense_thread(void);

/**
 * Get the USB mux thread.
 *
 * @return Thread ID, or NULL if there is no USB mux thread.
 */
k_tid_t get_usb_mux_thread(void);

#ifdef TEST_BUILD
/**
 * Set TASK_ID_TEST_RUNNER to current thread tid. Some functions that are tested
 * require to run in any task context.
 */
void set_test_runner_tid(void);

/**
 * Get the thread last registered with set_test_runner_tid().
 */
k_tid_t get_test_runner_thread(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_EC_TASKS_H_ */
