/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_CHIPSET_H_
#define ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_CHIPSET_H_

#include "hooks.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, system_can_boot_ap);

void mock_chipset_reset_state(void);
int mock_get_hook_notify_count(enum hook_type type);
void mock_clear_hook_notify_counts(void);

#endif /* ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_CHIPSET_H_ */
