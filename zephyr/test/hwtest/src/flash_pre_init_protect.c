/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "multistep_test.h"
#include "system.h"
#include "write_protect.h"

#ifdef CONFIG_SOC_FAMILY_STM32
#include <zephyr/devicetree.h>

#include <soc.h>

#define FLASH_DEVICE_REG DT_REG_ADDR(DT_CHOSEN(zephyr_flash_controller))

static bool flash_option_bytes_locked(void)
{
	FLASH_TypeDef *regs = (FLASH_TypeDef *)FLASH_DEVICE_REG;

	return regs->OPTCR & FLASH_OPTCR_OPTLOCK;
}
#endif

static int check_image_and_hardware_write_protect(void)
{
	bool wp;

	if (system_get_image_copy() != EC_IMAGE_RO) {
		ccprintf("This test only works when running RO\n");
		return -ENOTSUP;
	}

	wp = write_protect_is_asserted();

	if (!wp) {
		ccprintf("Hardware write protect (GPIO_WP) must be enabled\n");
		return -ENOTSUP;
	}

	return 0;
}

static void test_wp_enable(void)
{
	int rv;

	zassert_equal(check_image_and_hardware_write_protect(), 0);

	/*
	 * Enable both RO_AT_BOOT and RO_NOW.
	 * This ensures that on the subsequent reboot into Step 2,
	 * RO_NOW is already active when cros_flash_driver_init() runs.
	 */
	rv = crec_flash_set_protect(
		EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW,
		EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW);

	zassert_equal(rv, EC_SUCCESS);
	cflush();
	system_reset(SYSTEM_RESET_HARD);
}

static void test_wp_disable(void)
{
	int rv;

	zassert_equal(check_image_and_hardware_write_protect(), 0);

	/* Equivalent of ectool --name=cros_fp flashprotect disable */
	rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT, 0);
	zassert_not_equal(rv, EC_SUCCESS);

	/*
	 * Directly invoking the physical layer must fail because option bytes
	 * were disabled during cros_flash_driver_init().
	 */
	rv = crec_flash_physical_protect_at_boot(0);
	zassert_not_equal(rv, EC_SUCCESS);

#ifdef CONFIG_SOC_FAMILY_STM32
	zassert_true(flash_option_bytes_locked());
#endif
}

static void (*test_steps[])(void) = { test_wp_enable, test_wp_disable };

MULTISTEP_TEST(flash_pre_init_protect, test_steps)
