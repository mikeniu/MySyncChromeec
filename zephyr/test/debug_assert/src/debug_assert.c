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
	struct panic_data *pdata;
	int linenum;
	const char *filename;

	filename = strrchr(__FILE__, '/') + 1;
	linenum = __LINE__ + 1;
	__ASSERT(false, "Test false assert");

	zassert_equal(system_reset_fake.call_count, 1);
	pdata = panic_get_data();
	zassert_not_null(pdata);
	zassert_equal(PANIC_SW_ASSERT, panic_get_reason_reg(pdata));
	if (!IS_ENABLED(CONFIG_ASSERT_NO_FILE_INFO)) {
		uint32_t info = panic_get_info_reg(pdata);

		zassert_equal(linenum, info & 0xffff);
		zassert_equal(filename[0], (info >> 24) & 0xff);
		zassert_equal(filename[1], (info >> 16) & 0xff);
	} else {
		zassert_equal(panic_get_info_reg(pdata), -1);
	}
	zassert_equal((uint8_t)(uintptr_t)k_current_get(),
		      panic_get_exception_reg(pdata));
}

ZTEST(debug_assert, test_assert_true)
{
	__ASSERT(true, "Test true assert");

	zassert_equal(system_reset_fake.call_count, 0);
	zassert_is_null(panic_get_data());
}

static void reset(void *data)
{
	ARG_UNUSED(data);
	/* clear panic data */
	memset(get_panic_data_write(), 0, sizeof(struct panic_data));
	RESET_FAKE(system_reset);
}

ZTEST_SUITE(debug_assert, NULL, NULL, reset, reset, NULL);
