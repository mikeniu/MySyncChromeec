/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "flash.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#if IS_ENABLED(CONFIG_PLATFORM_EC_EXTERNAL_STORAGE)
/*
 * Forward declarations for static functions exported for testing via
 * test_export_static.
 */
uint32_t unlock_rollback(void);
void lock_rollback(uint32_t key);

FAKE_VOID_FUNC(crec_flash_lock_mapped_storage, int);

static void rollback_lock_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(crec_flash_lock_mapped_storage);
}

ZTEST_SUITE(rollback_lock, NULL, NULL, rollback_lock_before, NULL, NULL);

ZTEST(rollback_lock, test_lock_matching)
{
	uint32_t key = unlock_rollback();
	lock_rollback(key);

	/* Ensure the function was called exactly twice. */
	zassert_equal(crec_flash_lock_mapped_storage_fake.call_count, 2,
		      "Expected exactly 2 mapped storage lock transitions");

	/* Ensure the first call was acquire (true) and second was release
	 * (false) */
	zassert_equal(crec_flash_lock_mapped_storage_fake.arg0_history[0], true,
		      "First transition should be an acquire (true)");
	zassert_equal(crec_flash_lock_mapped_storage_fake.arg0_history[1],
		      false, "Second transition should be a release (false)");
}
#else
ZTEST_SUITE(rollback_lock, NULL, NULL, NULL, NULL, NULL);

ZTEST(rollback_lock, test_lock_matching)
{
	ztest_test_skip();
}
#endif
