/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "task.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DECLARE_FAKE_VOID_FUNC(system_reset, int);
DEFINE_FAKE_VOID_FUNC(system_reset, int);

ZTEST(debug_assert, test_assert_false)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;
	int linenum;
	const char *filename;

	filename = strrchr(__FILE__, '/') + 1;
	linenum = __LINE__ + 1;
	__ASSERT(false, "Test false assert");

	zassert_equal(system_reset_fake.call_count, 1);
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(PANIC_SW_ASSERT, reason);
	if (!IS_ENABLED(CONFIG_ASSERT_NO_FILE_INFO)) {
		zassert_equal(linenum, info & 0xffff);
		zassert_equal(filename[0], (info >> 24) & 0xff);
		zassert_equal(filename[1], (info >> 16) & 0xff);
	} else {
		zassert_equal(info, -1);
	}
	zassert_equal((uint8_t)(uintptr_t)k_current_get(), exception);
}

ZTEST(debug_assert, test_assert_true)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	__ASSERT(true, "Test true assert");

	zassert_equal(system_reset_fake.call_count, 0);
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(0, reason);
	zassert_equal(0, info);
	zassert_equal(0, exception);
}

static void reset(void *data)
{
	ARG_UNUSED(data);
	/* clear panic data */
	memset(get_panic_data_write(), 0, sizeof(struct panic_data));
	RESET_FAKE(system_reset);
}

ZTEST_SUITE(debug_assert, NULL, NULL, reset, reset, NULL);
