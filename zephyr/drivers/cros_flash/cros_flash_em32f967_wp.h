/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_EM32F967_WP_H_
#define ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_EM32F967_WP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Check whether a specific flash bank is write-protected.
 *
 * This function reports the current hardware protection state of a given
 * flash bank on the EM32F967 platform. The protection state reflects whether
 * write or erase operations to the specified bank are blocked by the
 * underlying flash protection configuration.
 *
 * @param bank_index Index of the flash bank to check.
 *
 * @return true if the specified bank is currently protected against write/
 *         erase operations, false otherwise.
 */
bool flash_em32_check_bank_protected(int bank_index);

/**
 * @brief Enable the first write-protection range.
 *
 * This function enables the first flash write-protection range starting from
 * the given offset and covering the specified number of pages. The exact
 * hardware mapping of this protection range is platform-specific.
 *
 * @param offset      Byte offset within flash where the protection range
 * starts.
 * @param page_count  Number of flash pages to protect starting from offset.
 *
 * @return true on successful configuration of the protection range,
 *         false on failure.
 */
bool flash_em32_write_protect_1(uint32_t offset, size_t page_count);

/**
 * @brief Enable the second write-protection range.
 *
 * This function enables the second flash write-protection range starting from
 * the given offset and covering the specified number of pages. This range is
 * typically used to extend protection beyond the region covered by the first
 * protection range.
 *
 * @param offset      Byte offset within flash where the protection range
 * starts.
 * @param page_count  Number of flash pages to protect starting from offset.
 *
 * @return true on successful configuration of the protection range,
 *         false on failure.
 */
bool flash_em32_write_protect_2(uint32_t offset, size_t page_count);

/**
 * @brief Disable the first write-protection range.
 *
 * This function disables the first flash write-protection range, allowing
 * write and erase operations to the region previously protected by that range.
 */
void flash_em32_write_protect_1_disable(void);

/**
 * @brief Disable the second write-protection range.
 *
 * This function disables the second flash write-protection range, allowing
 * write and erase operations to the region previously protected by that range.
 */
void flash_em32_write_protect_2_disable(void);

#endif /* ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_EM32F967_WP_H_ */
