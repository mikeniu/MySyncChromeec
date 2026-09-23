/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_CHIP_RTK_INCLUDE_FLASH_CHIP_H_
#define PLATFORM_EC_ZEPHYR_SHIM_CHIP_RTK_INCLUDE_FLASH_CHIP_H_

#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/*
 * One page program instruction allows maximum 256 bytes (a page) of data
 * to be programmed.
 */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256
/* Minimum write size */
#define CONFIG_FLASH_WRITE_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), write_block_size)
/* Erase bank size */
#define CONFIG_FLASH_ERASE_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), erase_block_size)
/* Protect bank size, set by SPI_FLASH_SR1_SEC (0 = 64 KB) */
#define CONFIG_FLASH_BANK_SIZE 0x10000

#define CONFIG_RO_STORAGE_OFF 0x20
#define CONFIG_RW_STORAGE_OFF 0x0

#define BBRAM_WP_FLAG_INVALID 0xFFFFFFFF

#endif /* PLATFORM_EC_ZEPHYR_SHIM_CHIP_RTK_INCLUDE_FLASH_CHIP_H_ */
