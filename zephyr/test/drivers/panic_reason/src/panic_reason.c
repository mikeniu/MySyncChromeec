/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "task.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <sys/types.h>

ZTEST(panic_reason, test_panic_reason_zephyr)
{
	struct panic_data *pdata;

	k_sys_fatal_error_handler(K_ERR_KERNEL_PANIC, NULL);

	pdata = panic_get_data();
	zassert_not_null(pdata);
	zassert_equal(PANIC_ZEPHYR_FATAL_ERROR, panic_get_reason_reg(pdata));
	zassert_equal(K_ERR_KERNEL_PANIC, panic_get_info_reg(pdata));
	zassert_equal((uint8_t)(uintptr_t)k_current_get(),
		      panic_get_exception_reg(pdata));
}

ZTEST(panic_reason, test_panic_reason_zephyr_with_esf)
{
	struct arch_esf esf = {
		.dummy = 0x12345678, /* nocheck */
	};
	struct panic_data *pdata;

	k_sys_fatal_error_handler(K_ERR_KERNEL_PANIC, &esf);

	pdata = panic_get_data();
	zassert_not_null(pdata, NULL);
	zassert_equal(PANIC_ARCH_POSIX, pdata->arch);
	zassert_equal(0x12345678, pdata->posix.esf_placeholder);
	zassert_equal(PANIC_DATA_MAGIC, pdata->magic);
}

ZTEST_SUITE(panic_reason, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
