/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_interface.h"
#include "ec_app_main.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(ap_pwrseq_task_start);

/*
 * Verify ec_app_main() starts the AP power sequence task when
 * CONFIG_QCOM_AP_PWRSEQ_DRIVER is enabled.
 */
ZTEST(qcom_ap_pwrseq, test_ec_app_main)
{
	ec_app_main();
	zassert_equal(ap_pwrseq_task_start_fake.call_count, 1);
}

ZTEST_SUITE(qcom_ap_pwrseq, NULL, NULL, NULL, NULL, NULL);
