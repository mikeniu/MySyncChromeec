/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(ftrapv_hw_test, LOG_LEVEL_INF);

/*
 * Global variable to store the address of the function expected to cause a fault.
 * Used by the fatal error hook to verify the program counter.
 */
static void *expected_fault_addr;

ZTEST_SUITE(ftrapv, NULL, NULL, NULL, NULL, NULL);

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	/* Verify that the reason is a CPU exception or kernel panic */
	/* Note: We accept expected faults if expected_fault_addr is set. */

	if (expected_fault_addr) {
#if defined(CONFIG_ARM)
		/* Estimated end of a function. */
		uint32_t fn_end = (uint32_t)expected_fault_addr + 0x40;
		uint32_t pc = pEsf->basic.pc;

		/* Make sure Program Counter is stored correctly and points at a
		 * function that causes a crash. */
		zassert_true(((uint32_t)expected_fault_addr <= pc) && (fn_end >= pc),
			     "PC 0x%x not in range [0x%x, 0x%x]", pc,
			     (uint32_t)expected_fault_addr, fn_end);
#elif defined(CONFIG_RISCV)
		/* Estimated end of a function. */
		uint32_t fn_end = (uint32_t)expected_fault_addr + 0x40;
		uint32_t pc = pEsf->mepc;

		/* Make sure Program Counter is stored correctly and points at a
		 * function that causes a crash. */
		zassert_true(((uint32_t)expected_fault_addr <= pc) && (fn_end >= pc),
			     "PC 0x%x not in range [0x%x, 0x%x]", pc,
			     (uint32_t)expected_fault_addr, fn_end);
#endif

		/* Clear expectation and mark fault as valid (handled) */
		expected_fault_addr = NULL;
		ztest_set_fault_valid(false);
	} else {
		/* Unexpected fault, treat as error */
		ztest_set_fault_valid(false);
	}
}

/*
 * trapping addition: __addvsi3.
 */
static void __attribute__((noinline)) do_trapv_addition(void)
{
	int32_t test_overflow = INT32_MAX;
	int32_t ret;

	LOG_INF("Testing signed integer addition overflow");
	cflush();
	ret = test_overflow + 1;
	(void)ret;

	/* Should never reach this. */
	zassert_unreachable();
}

ZTEST(ftrapv, test_trapv_addition)
{
	expected_fault_addr = (void *)do_trapv_addition;
	ztest_set_fault_valid(true);
	do_trapv_addition();
}

/*
 * trapping subtraction: __subvsi3.
 */
static void __attribute__((noinline)) do_ftrapv_subtraction(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	LOG_INF("Testing signed integer subtraction overflow");
	cflush();
	ret = test_overflow - 1;
	(void)ret;

	/* Should never reach this. */
	zassert_unreachable();
}

ZTEST(ftrapv, test_ftrapv_subtraction)
{
	expected_fault_addr = (void *)do_ftrapv_subtraction;
	ztest_set_fault_valid(true);
	do_ftrapv_subtraction();
}

/*
 * trapping multiplication: __mulvsi3.
 */
static void __attribute__((noinline)) do_ftrapv_multiplication(void)
{
	int32_t test_overflow = INT32_MAX;
	int32_t ret;

	LOG_INF("Testing signed integer multiplication overflow");
	cflush();
	ret = test_overflow * 2;
	(void)ret;

	/* Should never reach this. */
	zassert_unreachable();
}

ZTEST(ftrapv, test_ftrapv_multiplication)
{
	expected_fault_addr = (void *)do_ftrapv_multiplication;
	ztest_set_fault_valid(true);
	do_ftrapv_multiplication();
}

/*
 * trapping negation: __negvsi2.
 */
static void __attribute__((noinline)) do_ftrapv_negation(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	LOG_INF("Testing signed integer negation overflow");
	cflush();
	ret = -test_overflow;
	(void)ret;

	/* Should never reach this. */
	zassert_unreachable();
}

ZTEST(ftrapv, test_ftrapv_negation)
{
	expected_fault_addr = (void *)do_ftrapv_negation;
	ztest_set_fault_valid(true);
	do_ftrapv_negation();
}

/*
 * trapping absolute value: __absvsi2.
 *
 * TODO(b/258074414): Trapping on absolute value overflow is broken in clang.
 */
static void __attribute__((noinline)) do_ftrapv_abs(void)
{
	int32_t test_overflow = INT32_MIN;
	int32_t ret;

	LOG_INF("Testing signed integer absolute value overflow\n");
	cflush();

	ret = abs(test_overflow);
	(void)ret;

	/* Should never reach this. */
	zassert_unreachable();
}

ZTEST(ftrapv, test_ftrapv_abs)
{
	expected_fault_addr = (void *)do_ftrapv_abs;
	ztest_set_fault_valid(true);
	do_ftrapv_abs();
}
