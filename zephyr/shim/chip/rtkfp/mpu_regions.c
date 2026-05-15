/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include <zephyr/devicetree.h>
#include <zephyr/arch/arm/mpu/arm_mpu_mem_cfg.h>

/*
 * For ARMv8-M, two consecutive rollbacks can be protected
 * using a single MPU region.
 */
BUILD_ASSERT((DT_REG_ADDR(DT_NODELABEL(rollback0)) +
			DT_REG_SIZE(DT_NODELABEL(rollback0)) ==
			DT_REG_ADDR(DT_NODELABEL(rollback1))),
			"The two rollback regions need to be consecutive.");

#define REGION_PPB_BASE	0xE0000000
#define REGION_PPB_SIZE	0x100000
#define REGION_PERIPHERAL_BASE	0x40000000
#define REGION_PERIPHERAL_SIZE	0x200000

#define REGION_FLASH_WP_RO_BASE (CONFIG_MAPPED_STORAGE_BASE + \
				 DT_REG_ADDR(DT_NODELABEL(wp_ro)))
#define REGION_FLASH_WP_RO_SIZE DT_REG_SIZE(DT_NODELABEL(wp_ro))
#define REGION_FLASH_ROLLBACK_BASE (CONFIG_MAPPED_STORAGE_BASE + \
				    DT_REG_ADDR(DT_NODELABEL(rollback0)))
#define REGION_FLASH_ROLLBACK_SIZE (DT_REG_SIZE(DT_NODELABEL(rollback0)) + \
				    DT_REG_SIZE(DT_NODELABEL(rollback1)))
#define REGION_FLASH_EC_RW_BASE (CONFIG_MAPPED_STORAGE_BASE + \
				 DT_REG_ADDR(DT_NODELABEL(ec_rw)))
#define REGION_FLASH_EC_RW_SIZE DT_REG_SIZE(DT_NODELABEL(ec_rw))

#define REGION_SRAM_BASE	0x20000000
#define REGION_SRAM_SIZE	0x40000

static const struct arm_mpu_region mpu_regions[] = {
	MPU_REGION_ENTRY("PPB", REGION_PPB_BASE,
			 REGION_DEVICE_ATTR(REGION_PPB_BASE, REGION_PPB_SIZE)),

	MPU_REGION_ENTRY("PERIPHERALS", REGION_PERIPHERAL_BASE,
			 REGION_DEVICE_ATTR(REGION_PERIPHERAL_BASE,
					    REGION_PERIPHERAL_SIZE)),

	MPU_REGION_ENTRY("WP_RO", REGION_FLASH_WP_RO_BASE,
			 REGION_FLASH_ATTR(REGION_FLASH_WP_RO_BASE,
					   REGION_FLASH_WP_RO_SIZE)),

	MPU_REGION_ENTRY("ROLLBACK", REGION_FLASH_ROLLBACK_BASE,
			 REGION_FLASH_ATTR(REGION_FLASH_ROLLBACK_BASE,
					   REGION_FLASH_ROLLBACK_SIZE)),

	MPU_REGION_ENTRY("EC_RW", REGION_FLASH_EC_RW_BASE,
			 REGION_FLASH_ATTR(REGION_FLASH_EC_RW_BASE,
					   REGION_FLASH_EC_RW_SIZE)),

	MPU_REGION_ENTRY("SRAM", REGION_SRAM_BASE,
			 REGION_RAM_ATTR(REGION_SRAM_BASE,
					 REGION_SRAM_SIZE)),
};

const struct arm_mpu_config mpu_config = {
	.num_regions = ARRAY_SIZE(mpu_regions),
	.mpu_regions = mpu_regions,
};
