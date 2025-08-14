/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FLASH_CHIP_H
#define __CROS_EC_FLASH_CHIP_H

/*
 * One page program instruction allows maximum 256 bytes (a page) of data
 * to be programmed.
 */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256
/* Minimum write size */
#define CONFIG_FLASH_WRITE_SIZE 1
/* Erase sector size */
#define CONFIG_FLASH_ERASE_SIZE 0x1000

#define CONFIG_RO_STORAGE_OFF 0x20
#define CONFIG_RW_STORAGE_OFF 0x0

#endif /* __CROS_EC_FLASH_CHIP_H */
