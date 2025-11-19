/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_EM32F967_WP_H_
#define ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_EM32F967_WP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool flash_em32_check_bank_protected(int bank_index);
bool flash_em32_write_protect_1(uint32_t offset, size_t page_count);
bool flash_em32_write_protect_2(uint32_t offset, size_t page_count);
void flash_em32_write_protect_1_disable(void);
void flash_em32_write_protect_2_disable(void);
#endif /* ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_EM32F967_WP_H_ */
