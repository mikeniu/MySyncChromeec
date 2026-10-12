/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "task_id.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>

/*
 * The fakes below are needed to satisfy the final link, but are never
 * actually called at runtime. Exclude from the coverage report to avoid
 * false positives on the low-coverage check.
 */
/* LCOV_EXCL_START */
FAKE_VALUE_FUNC(task_id_t, task_get_current);
FAKE_VOID_FUNC(task_set_event, task_id_t, uint32_t);
FAKE_VALUE_FUNC(uint32_t, task_wait_event, int);
FAKE_VALUE_FUNC(uint32_t, task_wait_event_mask, uint32_t, int);
FAKE_VALUE_FUNC(k_tid_t, get_motionsense_thread);
FAKE_VALUE_FUNC(k_tid_t, get_usb_mux_thread);
FAKE_VALUE_FUNC(k_tid_t, get_test_runner_thread);
FAKE_VALUE_FUNC(int, pd_get_current_thread_port);
/* LCOV_EXCL_STOP */
