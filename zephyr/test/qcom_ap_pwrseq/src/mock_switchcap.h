/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_SWITCHCAP_H_
#define ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_SWITCHCAP_H_

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, cros_switchcap_set_power, const struct device *,
			bool);
DECLARE_FAKE_VALUE_FUNC(bool, cros_switchcap_is_enabled, const struct device *);
DECLARE_FAKE_VALUE_FUNC(bool, cros_switchcap_is_power_good,
			const struct device *);

void mock_switchcap_reset_state(void);

#endif /* ZEPHYR_TEST_QCOM_AP_PWRSEQ_SRC_MOCK_SWITCHCAP_H_ */
