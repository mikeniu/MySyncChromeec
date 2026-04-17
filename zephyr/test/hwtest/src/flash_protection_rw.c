/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "system.h"
#include "write_protect.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_test.h>

LOG_MODULE_REGISTER(test_flash_protection_rw);

static void test_jump_to_ro(void)
{
		LOG_INF("I am in func: %s", __func__);
	cflush();

	zassert_equal(system_get_image_copy(), EC_IMAGE_RW,
		      "Test must start in RW to perform initial jump");

	LOG_INF("Jumping from RW to RO...");
	cflush();

	system_reset(SYSTEM_RESET_STAY_IN_RO);
}


static void test_set_ro_at_boot(void)
{
	LOG_INF("I am in func: %s", __func__);
	cflush();

		zassert_equal(system_get_image_copy(), EC_IMAGE_RO,
		      "Not in RO after reboot to RO");
	int rv;
	bool wp = write_protect_is_asserted();

	zassert_true(wp, "WP must be enabled");
	LOG_INF("WP enabled, setting RO_AT_BOOT");
	rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
				    EC_FLASH_PROTECT_RO_AT_BOOT);
	zassert_equal(rv, EC_SUCCESS);
	LOG_INF("RO_AT_BOOT set, rebooting");
	cflush();
	system_reset(SYSTEM_RESET_HARD);
}

static void test_check_all_now_in_rw(void)
{
	LOG_INF("I am in func: %s", __func__);
	cflush();
	zassert_equal(system_get_image_copy(), EC_IMAGE_RW,
		      "Not in RW after reboot");
	LOG_INF("Rebooted to RW");
	zassert_equal(crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW,
		      EC_FLASH_PROTECT_ALL_NOW, "ALL_NOW not set after reboot");
	LOG_INF("ALL_NOW set in RW, rebooting to RO");
	cflush();
	system_reset(SYSTEM_RESET_STAY_IN_RO);
}

static void test_check_all_now_in_ro(void)
{
	LOG_INF("I am in func: %s", __func__);
	cflush();
	zassert_equal(system_get_image_copy(), EC_IMAGE_RO,
		      "Not in RO after reboot");
	LOG_INF("Rebooted to RO");
	zassert_not_equal(crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW,
			  EC_FLASH_PROTECT_ALL_NOW,
			  "ALL_NOW set after reboot to RO");
	LOG_INF("ALL_NOW not set in RO");
}

static void (*test_steps[])(void) = { test_jump_to_ro, test_set_ro_at_boot,
				      test_check_all_now_in_rw,
				      test_check_all_now_in_ro };

/*
 * Use custom method of multistep testing, because steps have to be run in a
 * different image - 2 steps in RW, one step in RO. This is not possible with
 * the common pattern.
 */

/* Use a unique step 0 value. */
#define STEP_ZERO ((UINT16_MAX / 2) + 1)
#define TEST_STEP(step) (step - STEP_ZERO)

static struct k_work multistep_test_work;
static void multistep_test_handler(struct k_work *work)
{
	uint32_t step = 0;
	system_get_scratchpad(&step);
	int image = system_get_image_copy();

	LOG_INF("Handler triggering. Current Image: %s, Step: %u", 
	        (image == EC_IMAGE_RO ? "RO" : "RW"), step);
	cflush();

	if ((step >= STEP_ZERO) && (step < STEP_ZERO + ARRAY_SIZE(test_steps))) {
		int step_index = TEST_STEP(step);
		
		/* descriptive logging for debugging */
		LOG_INF("Evaluating Step Index %d for Image %s", 
		        step_index, (image == EC_IMAGE_RO ? "RO" : "RW"));

		/* 
		 * Corrected Logic: 
		 * Steps 1 and 3 are RO steps.
		 * Step 2 is an RW step.
		 */
		if (image == EC_IMAGE_RO) {
			if (step_index == 1 || step_index == 3) {
				LOG_INF("RO image running step %d", step_index);
			} else {
				LOG_WRN("RO image skipping step %d", step_index);
				return;
			}
		} else if (image == EC_IMAGE_RW) {
			if (step_index == 2) {
				LOG_INF("RW image running step %d", step_index);
			} else {
				LOG_WRN("RW image skipping step %d", step_index);
				return;
			}
		}

		LOG_INF("Triggering ztest_run_test_suite...");
		cflush();
		ztest_run_test_suite(flash_protection_rw, false, 1, 1, NULL);
	}
}

static int multistep_test_init(void)
{
	LOG_INF("I am in func: %s", __func__);
	cflush();
	
	k_work_init(&multistep_test_work, multistep_test_handler);

	/* Check if the test has to be run after reboot */
	k_work_submit(&multistep_test_work);

	return 0;
}
SYS_INIT(multistep_test_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

static void *flash_protection_rw_setup(void)
{
	LOG_INF("I am in func: %s", __func__);
	cflush();

	uint32_t step = 0;
	int ret = 0;

	ret = system_get_scratchpad(&step);
	if ((step < STEP_ZERO) ||
	    (step >= (STEP_ZERO + ARRAY_SIZE(test_steps)))) {
		system_set_scratchpad(STEP_ZERO);
	}
	zassert_equal(ret, 0);

	return NULL;
}

static void flash_protection_rw_teardown(void *fixture)
{
	LOG_INF("I am in func: %s", __func__);
	cflush();

	system_set_scratchpad(0);
}

ZTEST(flash_protection_rw, test_flash_protection_rw)
{
	LOG_INF("I am in func: %s", __func__);
	cflush();

	uint32_t step = 0;
	int ret = 0;

	ret = system_get_scratchpad(&step);
	zassert_equal(ret, 0);
	ret = system_set_scratchpad(step + 1);

	test_steps[TEST_STEP(step)]();
}
ZTEST_SUITE(flash_protection_rw, NULL, flash_protection_rw_setup, NULL, NULL,
	    flash_protection_rw_teardown);
