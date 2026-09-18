/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

namespace mock_chre
{

enum class Step {
	kNone,
	kRegisterTask,
	kInitCommon,
	kLateInit,
	kLoadNanoapps,
	kRun,
	kDeinitCommon,
};

struct State {
	struct k_sem &sem_started;
	struct k_sem &sem_allow_exit;
	struct k_sem &sem_finished;

	Step call_sequence[16];
	size_t call_count;
	k_tid_t thread_tid;
	uint32_t nanoapp_init_count;
};

State &getState();
void recordStep(Step step);

} // namespace mock_chre

#endif // ZEPHYR_TEST_CHRE_MOCK_CHRE_H_
