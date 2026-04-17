/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "multistep_test.h"
#include "system.h"
#include "write_protect.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_flash_protection_rw);

/**
 * @brief Step 0: Set RO_AT_BOOT and trigger a reboot.
 *
 * This step runs in RW. We set the EC_FLASH_PROTECT_RO_AT_BOOT flag, which
 * instructs the RO image (even a production/locked one) to enable full
 * flash protection (ALL_NOW) upon the next boot before jumping to RW.
 */
static void test_trigger_ro_at_boot(void)
{
	int rv;
	bool wp = write_protect_is_asserted();

	zassert_true(wp, "WP must be enabled");

	zassert_equal(system_get_image_copy(), EC_IMAGE_RW,
		      "Test must start in RW");

	LOG_INF("Step 0: Setting RO_AT_BOOT flag...");
	cflush();

	rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
				    EC_FLASH_PROTECT_RO_AT_BOOT);
	zassert_equal(rv, EC_SUCCESS, "Failed to set RO_AT_BOOT");

	LOG_INF("Triggering hard reset to pass through RO...");
	cflush();

	system_reset(SYSTEM_RESET_HARD);
}

/**
 * @brief Step 1: Verify protection state after returning to RW.
 *
 * After the reset in Step 0, the system boots into RO. The production RO
 * sees RO_AT_BOOT, enables protection (ALL_NOW), and jumps to RW.
 * This function verifies that the protection successfully persisted into RW.
 */
static void test_verify_protection_after_reboot(void)
{
	uint32_t prot = crec_flash_get_protect();

	zassert_equal(system_get_image_copy(), EC_IMAGE_RW,
		      "Must be back in RW for verification");

	LOG_INF("Step 1: Verifying ALL_NOW is active...");

	/*
	 * ALL_NOW should be set because the RO bootloader committed the
	 * protection before the jump.
	 */
	zassert_true((prot & EC_FLASH_PROTECT_ALL_NOW),
		     "ALL_NOW was not enabled by RO during boot");

	LOG_INF("Flash protection verified: 0x%08x", prot);
}

static void (*test_steps[])(void) = {
	test_trigger_ro_at_boot,
	test_verify_protection_after_reboot,
};

MULTISTEP_TEST(flash_protection_rw, test_steps);
