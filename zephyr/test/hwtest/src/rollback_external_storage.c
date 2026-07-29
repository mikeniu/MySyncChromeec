/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "rollback.h"

#include <zephyr/ztest.h>

static int unlock_rollback_call_count = 0;
static int lock_rollback_call_count = 0;

static const uint8_t FAKE_ENTROPY[] = { 0xff, 0xff, 0xff, 0xff };

/* Mock MPU/Rollback locking functions to track calls */
uint32_t unlock_rollback(void)
{
	unlock_rollback_call_count++;
	return 0x1234;
}

void lock_rollback(uint32_t key)
{
	lock_rollback_call_count++;
}

ZTEST_SUITE(rollback_external_storage, NULL, NULL, NULL, NULL, NULL);

ZTEST(rollback_external_storage, test_rollback_update_skips_mpu_unlock)
{
	unlock_rollback_call_count = 0;
	lock_rollback_call_count = 0;
	int rv;

	if (system_get_image_copy() != EC_IMAGE_RO) {
		ccprintf("This test is only works when running RO\n");
		zassert_unreachable();
	}

	/* Execute rollback update */
	if (IS_ENABLED(CONFIG_CROS_EC_RO)) {
		rv = rollback_add_entropy(FAKE_ENTROPY, sizeof(FAKE_ENTROPY));
		zassert_equal(rv, EC_SUCCESS);
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_EXTERNAL_STORAGE)) {
		/* MPU should NOT be unlocked or locked on external storage
		 * platforms */
		zassert_equal(
			unlock_rollback_call_count, 0,
			"unlock_rollback should not be called when external storage is enabled");
		zassert_equal(
			lock_rollback_call_count, 0,
			"lock_rollback should not be called when external storage is enabled");
	} else {
		/* On internal storage platforms, unlock/lock MUST be called */
		zassert_equal(
			unlock_rollback_call_count, 1,
			"unlock_rollback must be called once for internal storage");
		zassert_equal(
			lock_rollback_call_count, 1,
			"lock_rollback must be called once for internal storage");
	}
}
