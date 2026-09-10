/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_EMUL_INCLUDE_FLASH_CHIP_NATIVE_POSIX_H_
#define PLATFORM_EC_ZEPHYR_EMUL_INCLUDE_FLASH_CHIP_NATIVE_POSIX_H_

#define CONFIG_RO_STORAGE_OFF 0x0
#define CONFIG_RW_STORAGE_OFF 0x0
#define CONFIG_FLASH_WRITE_SIZE 0x1 /* minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256 /* one page size for write */
#define CONFIG_FLASH_ERASE_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), erase_block_size)
#define CONFIG_FLASH_BANK_SIZE CONFIG_FLASH_ERASE_SIZE

#endif /* PLATFORM_EC_ZEPHYR_EMUL_INCLUDE_FLASH_CHIP_NATIVE_POSIX_H_ */
