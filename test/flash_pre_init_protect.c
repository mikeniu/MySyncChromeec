/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chip/stm32/flash-regs.h"
#include "flash.h"
#include "gpio.h"
#include "panic.h"
#include "string.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "write_protect.h"

test_static int check_image_and_hardware_write_protect(void)
{
	bool wp;

	if (system_get_image_copy() != EC_IMAGE_RO) {
		ccprintf("This test only works when running RO\n");
		return EC_ERROR_UNKNOWN;
	}

	wp = write_protect_is_asserted();

	if (!wp) {
		ccprintf("Hardware write protect (GPIO_WP) must be enabled\n");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

test_static int test_flash_write_protect_enable(void)
{
	int rv;

	TEST_EQ(check_image_and_hardware_write_protect(), EC_SUCCESS, "%d");

	/*
	 * Enable both RO_AT_BOOT and RO_NOW.
	 * This ensures that on the subsequent reboot into Step 2,
	 * RO_NOW is already active when crec_flash_pre_init() runs.
	 */
	rv = crec_flash_set_protect(
		EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW,
		EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW);
	TEST_EQ(rv, EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static int test_flash_write_protect_disable(void)
{
	int rv;

	TEST_EQ(check_image_and_hardware_write_protect(), EC_SUCCESS, "%d");

	/* Equivalent of ectool --name=cros_fp flashprotect disable */
	rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT, 0);
	TEST_NE(rv, EC_SUCCESS, "%d");

	/*
	 * Directly invoking the physical layer must fail because option bytes
	 * were disabled during crec_flash_pre_init().
	 * Note: Returns EC_ERROR_ACCESS_DENIED on STM32H7 and EC_ERROR_UNKNOWN
	 * on STM32F because unlock() encounters locked option registers.
	 */
	rv = crec_flash_physical_protect_at_boot(0);
	TEST_NE(rv, EC_SUCCESS, "%d");

	TEST_ASSERT(flash_option_bytes_locked());

	return EC_SUCCESS;
}

test_static void run_test_step1(void)
{
	ccprintf("Step 1: Flash write protect test\n");
	RUN_TEST(test_flash_write_protect_enable);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_2);
}

test_static void run_test_step2(void)
{
	ccprintf("Step 2: Flash write protect test\n");
	RUN_TEST(test_flash_write_protect_disable);

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
	crec_msleep(30); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
