/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_CHIP_NPCX_INCLUDE_RAM_LOCK_H_
#define PLATFORM_EC_ZEPHYR_SHIM_CHIP_NPCX_INCLUDE_RAM_LOCK_H_

enum mpu_region {
	REGION_DATA_RAM = 0, /* For internal data RAM */
	REGION_DATA_RAM2 = 1, /* Second region for unaligned size */
	REGION_CODE_RAM = 2, /* For internal code RAM */
	REGION_CODE_RAM2 = 3, /* Second region for unaligned size */
	REGION_STORAGE = 4, /* For mapped internal storage */
	REGION_STORAGE2 = 5, /* Second region for unaligned size */
	REGION_DATA_RAM_TEXT = 6, /* Exempt region of data RAM */
	REGION_CHIP_RESERVED = 7, /* Reserved for use in chip/ */
	/* only for chips with MPU supporting 16 regions */
	REGION_UNCACHED_RAM = 8, /* For uncached data RAM */
	REGION_UNCACHED_RAM2 = 9, /* Second region for unaligned size */
	REGION_ROLLBACK = 10, /* For rollback */
};

int ram_lock_config_lock_region(uint8_t region, uint32_t addr, uint32_t size);

#endif /* PLATFORM_EC_ZEPHYR_SHIM_CHIP_NPCX_INCLUDE_RAM_LOCK_H_ */
