/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_CHIP_STM32_INCLUDE_FLASH_CHIP_H_
#define PLATFORM_EC_ZEPHYR_SHIM_CHIP_STM32_INCLUDE_FLASH_CHIP_H_

/* Minimum write size */
#define CONFIG_FLASH_WRITE_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), write_block_size)

/* No page mode, so use minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE CONFIG_FLASH_WRITE_SIZE

/* RO image offset inside protected storage (RO part) */
#define CONFIG_RO_STORAGE_OFF 0x0

/* RW image offset inside writable storage (RW part) */
#define CONFIG_RW_STORAGE_OFF 0x0

#endif /* PLATFORM_EC_ZEPHYR_SHIM_CHIP_STM32_INCLUDE_FLASH_CHIP_H_ */
