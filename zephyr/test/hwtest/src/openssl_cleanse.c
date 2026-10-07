/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Running these tests without optimization is pointless, since the primary
 * purpose of the OPENSSL_cleanse function is to evade compiler optimizations.
 * If optimization is disabled, the test_optimization_working test will fail.
 */

#include "common.h"
#include "openssl/mem.h"

#include <string.h>

#include <zephyr/ztest.h>

/* 256 bytes of stack is only safe enough for a memcpy. */
#define EXTRA_STACK_SIZE 256
#define BUFFER_SIZE 256
#define UNIQUE_STRING "Hello World!"

ZTEST_SUITE(openssl_cleanse, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Check basic memset behavior of OPENSSL_cleanse.
 */
ZTEST(openssl_cleanse, test_basic_functionality)
{
	static const char expected_zero_buf[BUFFER_SIZE] = { 0 };
	char buf[BUFFER_SIZE];

	for (size_t i = 0; i < sizeof(buf); i++) {
		buf[i] = (char)i;
	}

	OPENSSL_cleanse(buf, sizeof(buf));

	zassert_mem_equal(buf, expected_zero_buf, sizeof(buf));
}

/**
 * This function creates a contrived scenario where the compiler would choose
 * to optimize out the last memset due to no side effect.
 *
 * Disable ASAN with the attribute because this function returns a pointer to
 * stack memory, which is normally very naughty, but is what we want to test
 * here.
 */
__attribute__((noinline, no_sanitize("address"))) static void
exercise_memset(char **p)
{
	/*
	 * Add extra stack space so that |buf| doesn't get trampled while the
	 * caller is processing the |p| we set (ex. printing and testing p).
	 *
	 * Without volatile, space will be optimized out.
	 */
	volatile char __unused space[EXTRA_STACK_SIZE] = {
		[0 ... EXTRA_STACK_SIZE - 1] = 's'
	};

	char buf[] = UNIQUE_STRING;
	*p = buf;

	/*
	 * Force access to |buf| to ensure that it is allocated and seen as
	 * used. Without casting to volatile, the access would be optimized out.
	 * We don't want to make |buf| itself volatile, since
	 * we want the compiler to optimize out the final memset.
	 */
	for (size_t i = 0; i < sizeof(buf); i++)
		(void)((volatile char *)buf)[i];

	/* Expect the following memset to be omitted during optimization. */
	memset(buf, 0, sizeof(buf));
}

/**
 * Ensure that optimization is removing a trailing memset that it deems to have
 * no side-effect.
 */
ZTEST(openssl_cleanse, test_optimization_working)
{
	char buf[sizeof(UNIQUE_STRING)];
	char *p;

	exercise_memset(&p);
	memcpy(buf, p, sizeof(buf));

	/*
	 * We expect that the compiler would have optimized out the final
	 * memset, thus we should still be able to see the UNIQUE_STRING in
	 * memory.
	 */
	zassert_mem_equal(buf, UNIQUE_STRING, sizeof(buf));
}

/**
 * This function creates a contrived scenario where the compiler would choose
 * to optimize out the last memset due to no side effect.
 *
 * This function layout must remain identical to exercise_memset.
 *
 * Disable ASAN with the attribute because this function returns a pointer to
 * stack memory, which is normally very naughty, but is what we want to test
 * here.
 */
__attribute__((noinline, no_sanitize("address"))) static void
exercise_openssl_cleanse(char **p)
{
	/*
	 * Add extra stack space so that |buf| doesn't get trampled while the
	 * caller is processing the |p| we set (ex. printing and testing p).
	 *
	 * Without volatile, space will be optimized out.
	 */
	volatile char __unused space[EXTRA_STACK_SIZE] = {
		[0 ... EXTRA_STACK_SIZE - 1] = 's'
	};

	char buf[] = UNIQUE_STRING;
	*p = buf;

	/*
	 * Force access to |buf| to ensure that it is allocated and seen as
	 * used. Without casting to volatile, the access would be optimized out.
	 * We don't want to make |buf| itself volatile, since
	 * we want the compiler to optimize out the final memset.
	 */
	for (size_t i = 0; i < sizeof(buf); i++)
		(void)((volatile char *)buf)[i];

	/* Expect the following cleanse to NOT be omitted during optimization.
	 */
	OPENSSL_cleanse(buf, sizeof(buf));
}

/**
 * Ensure that OPENSSL_cleanse works when used in a scenario where a normal
 * memset would be removed.
 */
ZTEST(openssl_cleanse, test_openssl_cleanse)
{
	static const char expected_zero_buf[sizeof(UNIQUE_STRING)] = { 0 };
	char buf[sizeof(UNIQUE_STRING)];
	char *p;

	exercise_openssl_cleanse(&p);
	memcpy(buf, p, sizeof(buf));

	zassert_mem_equal(buf, expected_zero_buf, sizeof(buf));
}
