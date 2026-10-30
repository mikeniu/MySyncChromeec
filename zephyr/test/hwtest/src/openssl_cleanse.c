/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "openssl/mem.h"

#include <stdbool.h>
#include <string.h>

#include <zephyr/toolchain.h>
#include <zephyr/ztest.h>

/* 256 bytes of stack is only safe enough for a memcpy. */
#define EXTRA_STACK_SIZE 256
#define BUFFER_SIZE 256
#define UNIQUE_STRING "Hello World!"

static const char expected_zero_buf[BUFFER_SIZE];

ZTEST_SUITE(openssl_cleanse, NULL, NULL, NULL, NULL, NULL);

ZTEST(openssl_cleanse, test_basic_functionality)
{
	char buf[BUFFER_SIZE];

	for (size_t i = 0; i < sizeof(buf); i++) {
		buf[i] = (char)i;
	}

	OPENSSL_cleanse(buf, sizeof(buf));

	zassert_mem_equal(buf, expected_zero_buf, sizeof(buf));
}

/*
 * Create a scenario where the compiler optimizes out a trailing memset due to
 * dead-store elimination, but preserves OPENSSL_cleanse.
 *
 * Disable ASAN because this function intentionally returns a pointer to stack
 * memory to inspect the stack frame after return.
 */
__noinline __attribute__((no_sanitize("address"))) static void
run_memset(char **p, bool use_openssl_cleanse)
{
	/*
	 * Reserve extra stack space so |buf| is not clobbered while the caller
	 * copies from |*p|. Without volatile, this buffer is optimized out.
	 */
	volatile char __unused space[EXTRA_STACK_SIZE] = {
		[0 ... EXTRA_STACK_SIZE - 1] = 's'
	};

	char buf[] = UNIQUE_STRING;
	*p = buf;

	/*
	 * Force volatile reads of |buf| so its initialization is not optimized
	 * out without making |buf| itself volatile (which would prevent dead-
	 * store elimination of the trailing memset).
	 */
	for (size_t i = 0; i < sizeof(buf); i++) {
		(void)((volatile char *)buf)[i];
	}

	if (use_openssl_cleanse) {
		OPENSSL_cleanse(buf, sizeof(buf));
	} else {
		memset(buf, 0, sizeof(buf));
	}
}

ZTEST(openssl_cleanse, test_optimization_working)
{
	char buf[sizeof(UNIQUE_STRING)];
	char *p;

	if (IS_ENABLED(CONFIG_NO_OPTIMIZATIONS)) {
		ztest_test_skip();
	}

	run_memset(&p, false);
	memcpy(buf, p, sizeof(buf));

	/*
	 * The trailing memset should be eliminated by the compiler, leaving
	 * UNIQUE_STRING intact on the stack.
	 */
	zassert_mem_equal(buf, UNIQUE_STRING, sizeof(buf));
}

ZTEST(openssl_cleanse, test_openssl_cleanse)
{
	char buf[sizeof(UNIQUE_STRING)];
	char *p;

	run_memset(&p, true);
	memcpy(buf, p, sizeof(buf));

	zassert_mem_equal(buf, expected_zero_buf, sizeof(buf));
}
