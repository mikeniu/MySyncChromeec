/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test_new.h>

#include <pw_log_zephyr/log_zephyr.h>
#include <pw_log/log.h>

ZTEST_SUITE(pigweed, NULL, NULL, NULL, NULL, NULL);

ZTEST(pigweed, log)
{
	PW_LOG_DEBUG("PW_LOG_DBG");
	PW_LOG_INFO("PW_LOG_INFO");
	PW_LOG_WARN("PW_LOG_ERR");
	PW_LOG_ERROR("PW_LOG_WARN");
}
