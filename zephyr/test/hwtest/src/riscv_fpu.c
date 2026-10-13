/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fenv.h>
#include <float.h>
#include <math.h>

#include <zephyr/arch/riscv/csr.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(riscv_fpu, NULL, NULL, NULL, NULL, NULL);

static void reset_fflags(void)
{
	csr_write(fflags, 0);
}

/* Performs division without casting to double. */
static float divf(float a, float b)
{
	float result;

	__asm__ volatile("fdiv.s %0, %1, %2" : "=f"(result) : "f"(a), "f"(b));

	return result;
}

/* Make sure to use FPU instead of a hardcoded value computed at build time. */
static inline float _sqrtf(float v)
{
	float root;

	__asm__ volatile("fsqrt.s %0, %1" : "=f"(root) : "f"(v));

	return root;
}

/*
 * Expect underflow when dividing the smallest number that can be represented
 * using floats.
 */
ZTEST(riscv_fpu, test_underflow)
{
	float result;

	reset_fflags();
	result = divf(FLT_TRUE_MIN, 2.0f);

	zassert_equal(result, 0.0f);
	zassert_equal(csr_read(fflags), FE_UNDERFLOW | FE_INEXACT);
}

/*
 * Expect overflow when dividing the highest number that can be represented
 * using floats by number smaller than < 1.0f.
 */
ZTEST(riscv_fpu, test_overflow)
{
	float result;

	reset_fflags();
	result = divf(FLT_MAX, 0.5f);

	zassert_true(isinf(result));
	zassert_equal(csr_read(fflags), FE_OVERFLOW | FE_INEXACT);
}

/* Expect Division By Zero exception when 1.0f/0.0f. */
ZTEST(riscv_fpu, test_division_by_zero)
{
	float result;

	reset_fflags();
	result = divf(1.0f, 0.0f);

	zassert_true(isinf(result));
	zassert_equal(csr_read(fflags), FE_DIVBYZERO);
}

/* Expect Invalid Operation when trying to get square root of -1.0f. */
ZTEST(riscv_fpu, test_invalid_operation)
{
	float result;

	reset_fflags();
	result = _sqrtf(-1.0f);

	zassert_true(isnan(result));
	zassert_equal(csr_read(fflags), FE_INVALID);
}

/* Expect Inexact bit to be set when performing 2.0f/3.0f. */
ZTEST(riscv_fpu, test_inexact)
{
	float result;

	reset_fflags();
	result = divf(2.0f, 3.0f);

	/* Check if result is not NaN nor infinity. */
	zassert_true(isfinite(result));
	zassert_equal(csr_read(fflags), FE_INEXACT);
}
