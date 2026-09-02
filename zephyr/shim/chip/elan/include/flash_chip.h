/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_CHIP_ELAN_INCLUDE_FLASH_CHIP_H_
#define ZEPHYR_SHIM_CHIP_ELAN_INCLUDE_FLASH_CHIP_H_

/* Minimum write size */
#define CONFIG_FLASH_WRITE_SIZE 16

/* No page mode, so use minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE CONFIG_FLASH_WRITE_SIZE

/* RO image offset inside protected storage (RO part) */
#define CONFIG_RO_STORAGE_OFF 0x0

/* RW image offset inside writable storage (RW part) */
#define CONFIG_RW_STORAGE_OFF 0x0

#define CONFIG_FLASH_ERASE_SIZE 8192
#define CONFIG_FLASH_BANK_SIZE 8192

#endif /* ZEPHYR_SHIM_CHIP_ELAN_INCLUDE_FLASH_CHIP_H_ */
