/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "mpu.h"
#include "system.h"

#include <stdbool.h>

#include <zephyr/arch/arm/mpu/arm_mpu.h>
#include <zephyr/arch/arm/mpu/arm_mpu_mem_cfg.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <cmsis_core.h>

LOG_MODULE_REGISTER(mpu_hwtest, LOG_LEVEL_INF);

struct mpu_info {
	int num_mpu_regions;
	bool mpu_is_unified;
};

#if defined(CONFIG_ARM)
static const struct mpu_info mpu_info = {
	.num_mpu_regions = 8,
	.mpu_is_unified = true,
};
#else
#error "MPU info not defined for this architecture. Please add it."
#endif

static void check_mpu_info(void)
{
	uint32_t dregion = (MPU->TYPE & MPU_TYPE_DREGION_Msk) >>
			   MPU_TYPE_DREGION_Pos;
	bool is_unified = (MPU->TYPE & MPU_TYPE_SEPARATE_Msk) == 0;
	bool is_enabled = (MPU->CTRL & MPU_CTRL_ENABLE_Msk) != 0;

	zassert_equal(dregion, mpu_info.num_mpu_regions);
	zassert_equal(is_unified, mpu_info.mpu_is_unified);
	zassert_true(is_enabled);
}

static void check_mpu_enable(void)
{
	bool found_flash_region = false;
	bool found_sram_region = false;
	uint32_t expected_flash_ap = IS_ENABLED(CONFIG_MPU_ALLOW_FLASH_WRITE) ?
					     P_RW_U_RO_Msk :
					     P_RO_U_RO_Msk;

	mpu_enable();

	for (uint32_t index = 0; index < mpu_config.num_regions; index++) {
		uint32_t base;
		uint32_t rasr;

		MPU->RNR = index;
		base = MPU->RBAR & MPU_RBAR_ADDR_Msk;
		rasr = MPU->RASR;

		zassert_true((rasr & MPU_RASR_ENABLE_Msk) != 0);
		zassert_equal(base, mpu_config.mpu_regions[index].base);
		zassert_equal(rasr & ~MPU_RASR_ENABLE_Msk,
			      mpu_config.mpu_regions[index].attr.rasr);

		if (base == CONFIG_FLASH_BASE_ADDRESS) {
			found_flash_region = true;
			/* Flash/code region must be executable. */
			zassert_equal(rasr & MPU_RASR_AP_Msk,
				      expected_flash_ap);
			zassert_equal(rasr & MPU_RASR_XN_Msk, 0);
		} else if (base == CONFIG_SRAM_BASE_ADDRESS) {
			found_sram_region = true;
			/* Data SRAM region must be read-write and
			 * execute-never.
			 */
			zassert_equal(rasr & MPU_RASR_AP_Msk, P_RW_U_NA_Msk);
			zassert_true((rasr & MPU_RASR_XN_Msk) != 0);
		}
	}

	zassert_true(found_flash_region);
	zassert_true(found_sram_region);
}

static void check_mpu_lock_rollback(void)
{
#ifdef CONFIG_PLATFORM_EC_ROLLBACK_MPU_PROTECT
	zassert_equal(mpu_lock_rollback(false), EC_SUCCESS);
	zassert_equal(mpu_lock_rollback(true), EC_SUCCESS);
#endif
}

static void check_mpu_data_access_violation(void)
{
	volatile char data;
	volatile char *const ram_base =
		(volatile char *const)CONFIG_CROS_EC_RAM_BASE;
	uint32_t region = mpu_info.num_mpu_regions - 1;

	/* Initial read should succeed before locking the region. */
	data = ram_base[0];
	(void)data;

	/*
	 * Configure the highest-priority MPU region as a 32-byte No-Access
	 * region at CONFIG_CROS_EC_RAM_BASE so it overrides SRAM_0.
	 */
	MPU->RBAR = ((uint32_t)ram_base & MPU_RBAR_ADDR_Msk) |
		    MPU_RBAR_VALID_Msk | region;
	MPU->RASR = REGION_32B | P_NA_U_NA_Msk | MPU_RASR_ENABLE_Msk;
	__DSB();
	__ISB();

	/*
	 * This faults with a MemManage data access violation at
	 * CONFIG_CROS_EC_RAM_BASE:
	 *
	 *   MMFAR Address: 0x<CONFIG_CROS_EC_RAM_BASE>
	 */
	data = ram_base[0];
	(void)data;

	zassert_unreachable();
}

ZTEST_SUITE(mpu, NULL, NULL, NULL, NULL, NULL);

ZTEST(mpu, test_mpu)
{
	if (system_get_image_copy() == EC_IMAGE_RW) {
		LOG_INF("Running MPU test in RW.");
	} else {
		LOG_INF("Running MPU test in RO.");
	}

	check_mpu_info();
	check_mpu_enable();
	check_mpu_lock_rollback();

	/* Must be last because it triggers a fatal MPU fault. */
	check_mpu_data_access_violation();
}
