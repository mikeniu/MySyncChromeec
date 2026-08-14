/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "flash.h"

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/ztest.h>

/*
 * Forward declarations for static functions exported for testing via
 * test_export_static.
 */
uint32_t unlock_rollback(void);
void lock_rollback(uint32_t key);

#if IS_ENABLED(CONFIG_PLATFORM_EC_EXTERNAL_STORAGE)
/*
 * Call counters to track mapped storage lock serialization on external
 * storage architectures (e.g., NPCX).
 */
static int g_mapped_storage_lock_count;
static int g_mapped_storage_unlock_count;

/*
 * Intercept crec_flash_lock_mapped_storage() to record lock acquisitions
 * and releases relative to MPU state transitions.
 */
void crec_flash_lock_mapped_storage(int lock)
{
	if (lock == 1) {
		g_mapped_storage_lock_count++;
	} else if (lock == 0) {
		g_mapped_storage_unlock_count++;
	}
}
#endif /* CONFIG_PLATFORM_EC_EXTERNAL_STORAGE */

ZTEST_SUITE(rollback_lock, NULL, NULL, NULL, NULL, NULL);

/*
 * Verify that unlock_rollback() acquires the mapped storage lock before
 * disabling MPU protection, and that lock_rollback() enables MPU protection
 * before releasing the mapped storage lock.
 */
ZTEST(rollback_lock, test_lock_matching)
{
#if IS_ENABLED(CONFIG_PLATFORM_EC_EXTERNAL_STORAGE)
	g_mapped_storage_lock_count = 0;
	g_mapped_storage_unlock_count = 0;

	uint32_t key = unlock_rollback();
	lock_rollback(key);

	zassert_equal(g_mapped_storage_lock_count, 1,
		      "Expected 1 lock acquisition in unlock_rollback");
	zassert_equal(g_mapped_storage_unlock_count, 1,
		      "Expected 1 lock release in lock_rollback");

	/* Ensure every lock acquisition has a matching release. */
	zassert_equal(g_mapped_storage_lock_count,
		      g_mapped_storage_unlock_count,
		      "Lock acquisitions (%d) and releases (%d) must match",
		      g_mapped_storage_lock_count,
		      g_mapped_storage_unlock_count);
#else
	/*
	 * On internal storage architectures (e.g., STM32/Bloonchipper),
	 * mapped storage locking is a no-op as flash reads/writes are
	 * inherently direct or unmapped.
	 */
	uint32_t key = unlock_rollback();

	lock_rollback(key);
#endif
}
