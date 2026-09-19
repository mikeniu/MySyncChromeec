/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_INPUT_H_
#define ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_INPUT_H_

#include <stdbool.h>

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, power_button_is_pressed);
DECLARE_FAKE_VALUE_FUNC(int, lid_is_open);

void mock_power_button_set(bool pressed);
void mock_lid_set(bool open);
void mock_input_reset_state(void);

#endif /* ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_INPUT_H_ */
