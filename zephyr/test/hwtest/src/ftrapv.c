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

static void *expected_fault_addr;
static void *expected_fault_end_addr;

ZTEST_SUITE(ftrapv, NULL, NULL, NULL, NULL, NULL);

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	zassert_true(reason == K_ERR_CPU_EXCEPTION);
	zassert_not_null(expected_fault_addr);
	zassert_not_null(expected_fault_end_addr);

	ztest_set_fault_valid(false);

#if defined(CONFIG_ARM)
	uint32_t pc = pEsf->basic.pc;
#elif defined(CONFIG_RISCV)
	uint32_t pc = pEsf->mepc;
#else
	uint32_t pc = 0;
	zassert_unreachable("Test not supported on this architecture");
#endif
	/* Make sure Program Counter is stored correctly and points at a
		* function that causes a crash. */
	zassert_true(((uint32_t)expected_fault_addr <= pc) &&
			     ((uint32_t)expected_fault_end_addr >= pc),
		     "PC 0x%x not in range [0x%x, 0x%x]", pc,
		     (uint32_t)expected_fault_addr,
		     (uint32_t)expected_fault_end_addr);

	expected_fault_addr = NULL;
	expected_fault_end_addr = NULL;
}

/*
 * trapping addition: __addvsi3.
 */
static void trapv_addition(void)
{
	volatile int32_t test_overflow = INT32_MAX;
	volatile int32_t ret;

	LOG_INF("Testing signed integer addition overflow");
	cflush();

	expected_fault_addr = &&trap_start;
	expected_fault_end_addr = &&trap_end;
	__asm__ volatile("" ::: "memory");

trap_start:
	ret = test_overflow + 1;
trap_end:
	__asm__ volatile("nop");

	/* Should never reach this. */
	zassert_unreachable();
	(void)ret;
}

ZTEST(ftrapv, test_trapv_addition)
{
	expected_fault_addr = NULL;
	expected_fault_end_addr = NULL;
	ztest_set_fault_valid(true);
	trapv_addition();
}

/*
 * trapping subtraction: __subvsi3.
 */
static void ftrapv_subtraction(void)
{
	volatile int32_t test_overflow = INT32_MIN;
	volatile int32_t ret;

	LOG_INF("Testing signed integer subtraction overflow");
	cflush();

	expected_fault_addr = &&trap_start;
	expected_fault_end_addr = &&trap_end;
	__asm__ volatile("" ::: "memory");

trap_start:
	ret = test_overflow - 1;
trap_end:
	__asm__ volatile("nop");

	/* Should never reach this. */
	zassert_unreachable();
	(void)ret;
}

ZTEST(ftrapv, test_ftrapv_subtraction)
{
	expected_fault_addr = NULL;
	expected_fault_end_addr = NULL;
	ztest_set_fault_valid(true);
	ftrapv_subtraction();
}

/*
 * trapping multiplication: __mulvsi3.
 */
static void ftrapv_multiplication(void)
{
	volatile int32_t test_overflow = INT32_MAX;
	volatile int32_t ret;

	LOG_INF("Testing signed integer multiplication overflow");
	cflush();

	expected_fault_addr = &&trap_start;
	expected_fault_end_addr = &&trap_end;
	__asm__ volatile("" ::: "memory");

trap_start:
	ret = test_overflow * 2;
trap_end:
	__asm__ volatile("nop");

	/* Should never reach this. */
	zassert_unreachable();
	(void)ret;
}

ZTEST(ftrapv, test_ftrapv_multiplication)
{
	expected_fault_addr = NULL;
	expected_fault_end_addr = NULL;
	ztest_set_fault_valid(true);
	ftrapv_multiplication();
}

/*
 * trapping negation: __negvsi2.
 */
static void ftrapv_negation(void)
{
	volatile int32_t test_overflow = INT32_MIN;
	volatile int32_t ret;

	LOG_INF("Testing signed integer negation overflow");
	cflush();

	expected_fault_addr = &&trap_start;
	expected_fault_end_addr = &&trap_end;
	__asm__ volatile("" ::: "memory");

trap_start:
	ret = -test_overflow;
trap_end:
	__asm__ volatile("nop");

	/* Should never reach this. */
	zassert_unreachable();
	(void)ret;
}

ZTEST(ftrapv, test_ftrapv_negation)
{
	expected_fault_addr = NULL;
	expected_fault_end_addr = NULL;
	ztest_set_fault_valid(true);
	ftrapv_negation();
}

/*
 * trapping absolute value: __absvsi2.
 *
 * TODO(b/258074414): Trapping on absolute value overflow is broken in clang.
 */
static void ftrapv_abs(void)
{
	volatile int32_t test_overflow = INT32_MIN;
	volatile int32_t ret;

	LOG_INF("Testing signed integer absolute value overflow\n");
	cflush();

	expected_fault_addr = &&trap_start;
	expected_fault_end_addr = &&trap_end;
	__asm__ volatile("" ::: "memory");

trap_start:
	ret = abs(test_overflow);
trap_end:
	__asm__ volatile("nop");

	/* Should never reach this. */
	zassert_unreachable();
	(void)ret;
}

ZTEST(ftrapv, test_ftrapv_abs)
{
	expected_fault_addr = NULL;
	expected_fault_end_addr = NULL;
	ztest_set_fault_valid(true);
	ftrapv_abs();
}
