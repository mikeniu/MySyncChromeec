/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_CHIP_NPCX_INCLUDE_FLASH_CHIP_H_
#define PLATFORM_EC_ZEPHYR_SHIM_CHIP_NPCX_INCLUDE_FLASH_CHIP_H_

#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

#define CONFIG_FLASH_WRITE_SIZE 0x1 /* minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256 /* one page size for write */
/*
 * This flash erase size is used for alignment check only. The actual erase
 * is done in the Zephyr upstream driver, which doesn't use this CONFIG value.
 * Instead, it dynamically picks the best erase size from 4KB(SE) or 64KB(BE).
 *
 * So use the smallest one for alignment check.
 */
#define CONFIG_FLASH_ERASE_SIZE CONFIG_CROS_FLASH_NPCX_ERASE_SIZE
/*
 * The smallest protection bank size is 1/8 of 512 KB or 1/16 of 1M flash,
 * i.e. 64KB.
 */
#define CONFIG_FLASH_BANK_SIZE 0x10000

/* RO image resides at start of protected region, right after header */
#define CONFIG_RO_STORAGE_OFF CONFIG_RO_HDR_SIZE

#define CONFIG_RW_STORAGE_OFF 0

#endif /* PLATFORM_EC_ZEPHYR_SHIM_CHIP_NPCX_INCLUDE_FLASH_CHIP_H_ */
