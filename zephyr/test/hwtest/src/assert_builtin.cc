/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "console.h"
#include "multistep_test.h"
#include "panic.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(assert_builtin_hw_test, LOG_LEVEL_INF);

static void test_assert(void)
{
	LOG_INF("Step 1: Run failed assertion.");
	cflush();
	/*
	 * NOTE: If this line number changes, update linenum in
	 * test_panic_data().
	 */
	ASSERT(0);
	zassert_unreachable();
}

static void test_panic_data(void)
{
	LOG_INF("Step 2: Verify that reboot happened due to PANIC_SW_ASSERT.");
	cflush();

	struct panic_data *pdata = panic_get_data();

	zassert_not_null(pdata);

	constexpr uint32_t expected_reason = PANIC_SW_ASSERT;
	/*
	 * The value of expected_info can be calculated as shown below:
	 * char fname[] = "assert_builtin.cc";
	 * int linenum = 23;
	 * uint32_t expected_info = (fname[0] << 24) | (fname[1] << 16) |
	 *                          (linenum & 0xffff);
	 */
	constexpr char fname[] = "assert_builtin.cc";
	constexpr int linenum = 23;
	constexpr uint32_t expected_info = (fname[0] << 24) | (fname[1] << 16) |
					   (linenum & 0xffff);
	const uint8_t expected_exception = (uint8_t)(uintptr_t)k_current_get();

	zassert_equal(panic_get_reason_reg(pdata), expected_reason);
	zassert_equal(panic_get_info_reg(pdata), expected_info);
	zassert_equal(panic_get_exception_reg(pdata), expected_exception);
}

static void (*test_steps[])(void) = { test_assert, test_panic_data };

MULTISTEP_TEST(assert_builtin, test_steps)
