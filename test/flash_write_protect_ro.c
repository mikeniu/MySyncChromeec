/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "flash.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "write_protect.h"

test_static int check_hw_write_protect_and_image(void)
{
	if (system_get_image_copy() != EC_IMAGE_RO) {
		ccprintf("This test only works when running RO\n");
		return EC_ERROR_UNKNOWN;
	}

	if (!write_protect_is_asserted()) {
		ccprintf("Hardware write protect (GPIO_WP) must be enabled\n");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

/*
 * Step 1: Enable RO protection at boot and reboot to load Option Bytes.
 */
test_static int test_step1_enable_ro_protection(void)
{
	int rv;

	TEST_EQ(check_hw_write_protect_and_image(), EC_SUCCESS, "%d");

	rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
				    EC_FLASH_PROTECT_RO_AT_BOOT);
	TEST_EQ(rv, EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static void run_test_step1(void)
{
	ccprintf("Step 1: Enable RO protection at boot and reboot\n");
	RUN_TEST(test_step1_enable_ro_protection);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_2);
}

/*
 * Step 2: Executed after reboot with WP asserted and Option Bytes active.
 *
 * Test that crec_flash_pre_init() populated the driver's protected_blocks mask
 * by attempting to write to the RO region.
 *
 * - With patch: crec_flash_physical_protect_now(false) was called,
 *   so crec_flash_physical_write() returns EC_ERROR_ACCESS_DENIED.
 * - Without patch: protected_blocks is 0, so crec_flash_physical_write()
 *   does not return EC_ERROR_ACCESS_DENIED.
 */
test_static int test_step2_verify_ro_write_denied(void)
{
	int rv;
	uint32_t dummy_data = 0x12345678;

	TEST_EQ(check_hw_write_protect_and_image(), EC_SUCCESS, "%d");

	/* Attempt physical write to bank 0 (RO region) */
	rv = crec_flash_physical_write(0, sizeof(dummy_data),
				       (const char *)&dummy_data);

	/* Must be rejected by driver with EC_ERROR_ACCESS_DENIED */
	TEST_EQ(rv, EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static void run_test_step2(void)
{
	ccprintf("Step 2: Verify RO physical write is denied by pre_init\n");
	RUN_TEST(test_step2_verify_ro_write_denied);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_PASSED);
}

void test_run_step(uint32_t state)
{
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1))
		run_test_step1();
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2))
		run_test_step2();
}

int task_test(void *unused)
{
	test_run_multistep();
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	crec_msleep(30);
	task_wake(TASK_ID_TEST);
}
