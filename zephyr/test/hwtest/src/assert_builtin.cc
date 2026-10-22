/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "console.h"
#include "multistep_test.h"
#include "panic.h"

#include <zephyr/logging/log.h>

#include <cstdint>

LOG_MODULE_REGISTER(assert_builtin_hw_test, LOG_LEVEL_INF);

/*
 * All __LINE__ tokens in a single macro invocation expand to the line number
 * where the macro is invoked. Calling assert_or_get_line(true) triggers
 * ASSERT(0) at that line, and calling assert_or_get_line(false) returns the
 * exact same line number without relying on #line directives or line offsets.
 */
#define ASSERT_OR_GET_LINE(trigger) \
	do {                        \
		if (trigger) {      \
			ASSERT(0);  \
		}                   \
		return __LINE__;    \
	} while (0)

static int assert_or_get_line(bool trigger)
{
	ASSERT_OR_GET_LINE(trigger);
}

static void test_assert()
{
	LOG_INF("Step 1: Run failed assertion.");
	cflush();
	assert_or_get_line(true);
	/*
	 * Unlike ASSERT() (which invokes zassert_post_action() to record
	 * PANIC_SW_ASSERT and reboot), zassert_unreachable() invokes Ztest's
	 * ztest_test_fail(), which aborts the test thread, runs
	 * multisetep_test_teardown() to clear the scratchpad, and prints
	 * PROJECT EXECUTION FAILED without rebooting.
	 */
	zassert_unreachable();
}

static void test_panic_data()
{
	LOG_INF("Step 2: Verify that reboot happened due to PANIC_SW_ASSERT.");
	cflush();

	struct panic_data *pdata = panic_get_data();

	zassert_not_null(pdata);

	constexpr uint32_t expected_reason = PANIC_SW_ASSERT;
	constexpr char fname[] = "assert_builtin.cc";
	const int linenum = assert_or_get_line(false);
	const uint32_t expected_info = (fname[0] << 24) | (fname[1] << 16) |
				       (linenum & 0xffff);
	const uint8_t expected_exception = static_cast<uint8_t>(
		reinterpret_cast<uintptr_t>(k_current_get()));

	zassert_equal(panic_get_reason_reg(pdata), expected_reason);
	zassert_equal(panic_get_info_reg(pdata), expected_info);
	zassert_equal(panic_get_exception_reg(pdata), expected_exception);
}

static void (*test_steps[])() = { test_assert, test_panic_data };

MULTISTEP_TEST(assert_builtin, test_steps)
