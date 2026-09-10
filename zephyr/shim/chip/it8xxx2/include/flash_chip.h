/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_CHIP_IT8XXX2_INCLUDE_FLASH_CHIP_H_
#define PLATFORM_EC_ZEPHYR_SHIM_CHIP_IT8XXX2_INCLUDE_FLASH_CHIP_H_
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
/* Protect bank size */
#define CONFIG_FLASH_BANK_SIZE CONFIG_FLASH_ERASE_SIZE

#define CONFIG_RO_STORAGE_OFF 0x0
#define CONFIG_RW_STORAGE_OFF 0x0

/*
 * The EC uses the one bank of flash to emulate a SPI-like write protect
 * register with persistent state.
 */
#ifdef CONFIG_PLATFORM_EC_FLASH_PSTATE_BANK
#define CONFIG_FW_PSTATE_SIZE CONFIG_FLASH_BANK_SIZE
#define CONFIG_FW_PSTATE_OFF (CONFIG_RO_STORAGE_OFF + CONFIG_RO_SIZE)
#else
#define CONFIG_FW_PSTATE_SIZE 0
#endif /* CONFIG_PLATFORM_EC_FLASH_PSTATE_BANK */

#endif /* PLATFORM_EC_ZEPHYR_SHIM_CHIP_IT8XXX2_INCLUDE_FLASH_CHIP_H_ */
