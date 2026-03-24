<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(null_pointer, LOG_LEVEL_INF);

ZTEST_SUITE(null_pointer, NULL, NULL, NULL, NULL, NULL);

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	zassert_equal(reason, K_ERR_CPU_EXCEPTION);
	ztest_set_fault_valid(false);
}

void null_pointer_dereference(void)
{
	volatile uint32_t *null_ptr = NULL;

	ztest_set_fault_valid(true);
	LOG_INF("The value of null_ptr after dereferencing is: %d", *null_ptr);

	/* Should never reach this. */
	zassert_unreachable();
}

ZTEST(null_pointer, test_null_pointer_dereference)
{
	null_pointer_dereference();

	/* Should never reach this. */
	zassert_unreachable();
}
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "multistep_test.h"
#include "panic.h"
#include "system.h"
#include "util.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(null_pointer, LOG_LEVEL_INF);

static void test_null_pointer_dereference(void)
{
	volatile uint32_t *null_ptr = NULL;
	LOG_INF("The value of null_ptr after dereferencing is: %d", *null_ptr);

	/* Should never reach this. */
	zassert_unreachable();
}

static void test_panic_data(void)
{
#ifdef CONFIG_ARM
	struct panic_data *const pdata = panic_get_data();
	uint32_t dereference_addr = (uint32_t)test_null_pointer_dereference;
	/* Estimated end of the test_null_pointer_dereference() function */
	uint32_t dereference_end =
		(uint32_t)test_null_pointer_dereference + 0x40;
	uint32_t pc = pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_PC];

	/* Make sure Program Counter is stored correctly and points at the abort
	 * function.
	 */
	zassert_true((dereference_addr <= pc) && (dereference_end >= pc));
#endif
}

static void (*test_steps[])(void) = { test_null_pointer_dereference,
				      test_panic_data };

MULTISTEP_TEST(null_pointer_dereference, test_steps)
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
