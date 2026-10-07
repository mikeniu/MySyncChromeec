/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/toolchain.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(assert_stdlib_hw_test, LOG_LEVEL_INF);

static bool expect_abort;
static bool abort_called;

ZTEST_SUITE(assert_stdlib, NULL, NULL, NULL, NULL, NULL);

extern FUNC_NORETURN void __real_abort(void);

/*
 * Intercept abort() via -Wl,--wrap=abort to verify that standard library
 * assert() invokes abort() before triggering a kernel panic.
 */
FUNC_NORETURN __used void __wrap_abort(void)
{
	if (expect_abort) {
		abort_called = true;
	}
	__real_abort();
}

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	ARG_UNUSED(pEsf);

	zassert_equal(reason, K_ERR_KERNEL_PANIC);
	zassert_true(expect_abort, "Unexpected fatal error");
	zassert_true(abort_called, "abort() was not called by assert()");

	ztest_set_fault_valid(false);
	expect_abort = false;
	abort_called = false;
}

ZTEST(assert_stdlib, test_assert_stdlib)
{
	expect_abort = true;
	abort_called = false;
	ztest_set_fault_valid(true);

	LOG_INF("Calling stdlib assert(false)");
	cflush();
	assert(false);

	zassert_unreachable();
}
