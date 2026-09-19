/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_CHIPSET_H_
#define ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_CHIPSET_H_

#include "hooks.h"

#include <stdbool.h>
#include <stdint.h>

extern bool mock_can_boot_ap;
extern uint32_t mock_reset_flags;
extern int mock_hook_notify_count[HOOK_TYPE_COUNT];

void mock_chipset_reset_state(void);
int mock_get_hook_notify_count(enum hook_type type);
void mock_clear_hook_notify_counts(void);

#endif /* ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_CHIPSET_H_ */
