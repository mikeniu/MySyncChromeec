/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(assert_stdlib_hw_test, LOG_LEVEL_INF);

static uintptr_t expected_fault_addr;

ZTEST_SUITE(assert_stdlib, NULL, NULL, NULL, NULL, NULL);

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	zassert_equal(reason, K_ERR_KERNEL_PANIC);
	zassert_not_equal(expected_fault_addr, 0,
			  "Expected fault address not set");

	ztest_set_fault_valid(false);

	/* Estimated end of the abort function, which is short. */
	const uintptr_t abort_func_estimated_size = 0x40;
	uintptr_t fn_end = expected_fault_addr + abort_func_estimated_size;
#if defined(CONFIG_ARM)
	uintptr_t pc = pEsf->basic.pc;
#elif defined(CONFIG_RISCV)
	uintptr_t pc = pEsf->mepc;
#else
	uintptr_t pc = 0;
	zassert_unreachable("Test not supported on this architecture");
#endif

	/*
	 * Standard C library assert() calls __assert_func(), which invokes
	 * abort(). Verify that the Program Counter points within abort().
	 */
	zassert_true(pc >= expected_fault_addr && (pc <= fn_end),
		     "PC 0x%" PRIxPTR " not in range [0x%" PRIxPTR
		     ", 0x%" PRIxPTR "]",
		     pc, expected_fault_addr, fn_end);

	expected_fault_addr = 0;
}

ZTEST(assert_stdlib, test_assert_stdlib)
{
	expected_fault_addr = (uintptr_t)abort;
	ztest_set_fault_valid(true);

	LOG_INF("Calling stdlib assert(false)");
	cflush();
	assert(false);

	zassert_unreachable();
}
