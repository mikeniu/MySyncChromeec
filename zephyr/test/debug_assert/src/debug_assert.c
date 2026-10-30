/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "task.h"

#include <setjmp.h>

#include <zephyr/fff.h>
#include <zephyr/sys/printk-hooks.h>
#include <zephyr/ztest.h>

DECLARE_FAKE_VOID_FUNC(system_reset, int);
DEFINE_FAKE_VOID_FUNC(system_reset, int);

extern bool in_assert;

/*
 * On real hardware, assertion handlers are FUNC_NORETURN: they lock interrupts
 * and call panic_reboot() -> system_reset(), which resets the EC and never
 * returns.
 *
 * To test assertion handling without returning into panic_reboot() and hitting
 * CODE_UNREACHABLE, EXPECT_NO_RETURN() saves the calling environment with
 * setjmp(reset_jmp) and the custom system_reset() fake unlocks interrupts and
 * unwinds the stack back to the test via longjmp(reset_jmp, 1).
 */
static jmp_buf reset_jmp;

static void system_reset_longjmp(int flags)
{
	ARG_UNUSED(flags);
	in_assert = false;
	irq_unlock(0);
	longjmp(reset_jmp, 1);
}

#define EXPECT_NO_RETURN(stmt)                                     \
	do {                                                       \
		if (setjmp(reset_jmp) == 0) {                      \
			stmt;                                      \
			zassert_unreachable("Expected no return"); \
		}                                                  \
	} while (0)

static void verify_assert_panic_data(int linenum)
{
	const char *filename = strrchr(__FILE__, '/') + 1;
	struct panic_data *pdata = panic_get_data();

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

ZTEST(debug_assert, test_assert_false)
{
	int linenum;

	linenum = __LINE__ + 1;
	EXPECT_NO_RETURN(__ASSERT(false, "Test false assert"));

	zassert_equal(system_reset_fake.call_count, 1);
	verify_assert_panic_data(linenum);
}

ZTEST(debug_assert, test_assert_true)
{
	__ASSERT(true, "Test true assert");

	zassert_equal(system_reset_fake.call_count, 0);
	zassert_is_null(panic_get_data());
}

static void system_reset_recursive_assert(int flags)
{
	/* Trigger a second assert while inside zassert_post_action(). */
	if (system_reset_fake.call_count == 1) {
		__ASSERT(false, "Second assert");
	}
	system_reset_longjmp(flags);
}

ZTEST(debug_assert, test_assert_recursive_post_action)
{
	int linenum;

	system_reset_fake.custom_fake = system_reset_recursive_assert;

	linenum = __LINE__ + 1;
	EXPECT_NO_RETURN(__ASSERT(false, "First assert"));

	zassert_equal(system_reset_fake.call_count, 2);
	verify_assert_panic_data(linenum);
}

static printk_hook_fn_t orig_printk_hook;
static bool printk_hook_installed;

static int recursive_printk_hook(int c)
{
	/* Trigger a second assert while inside zassert_fail(). */
	__ASSERT(false, "Second assert");
	if (orig_printk_hook != NULL) {
		return orig_printk_hook(c);
	}
	return c;
}

ZTEST(debug_assert, test_assert_recursive_print)
{
	int linenum;

	orig_printk_hook = __printk_get_hook();
	__printk_hook_install(recursive_printk_hook);
	printk_hook_installed = true;

	linenum = __LINE__ + 1;
	EXPECT_NO_RETURN(__ASSERT(false, "First assert"));

	zassert_equal(system_reset_fake.call_count, 1);
	verify_assert_panic_data(linenum);
}

static void reset(void *data)
{
	ARG_UNUSED(data);
	in_assert = false;
	if (printk_hook_installed) {
		__printk_hook_install(orig_printk_hook);
		printk_hook_installed = false;
	}
	/* clear panic data */
	memset(get_panic_data_write(), 0, sizeof(struct panic_data));
	RESET_FAKE(system_reset);
	system_reset_fake.custom_fake = system_reset_longjmp;
}

ZTEST_SUITE(debug_assert, NULL, NULL, reset, reset, NULL);
