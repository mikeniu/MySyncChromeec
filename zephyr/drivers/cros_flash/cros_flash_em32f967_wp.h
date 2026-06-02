/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_EM32F967_WP_H_
#define ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_EM32F967_WP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct flash_protect_range {
	uint32_t start; /* Inclusive start offset in bytes */
	uint32_t end; /* Exclusive end offset in bytes */
};

struct flash_prot_state {
	/* Inclusive start offset of the protected region */
	uint32_t start;

	/* Exclusive end offset of the protected region */
	uint32_t end;

	/*
	 * True if protection is configured.
	 * False means no hardware protection is active.
	 */
	bool enabled;
};

/**
 * @brief Get flash write-protection ranges configured in hardware.
 *
 * This function reports the flash protection ranges as configured by
 * the hardware protection registers on EM32F967.
 *
 * The hardware provides at most two protection ranges. Each range may
 * be absent (disabled) or valid. This function only reports raw hardware
 * state; no policy decisions or range aggregation are performed here.
 *
 * @param ranges Output array with space for exactly two elements.
 *               The caller must provide storage for both hardware
 *               protection ranges.
 *
 * @return Number of valid protection ranges (0, 1, or 2),
 *         or negative errno on failure.
 */
int flash_em32_get_protection_ranges(struct flash_protect_range ranges[2]);

/**
 * @brief Enable the first write-protection range.
 *
 * This function enables a flash write-protection range covering the
 * byte region [start, end).
 *
 * The input range must be aligned to hardware requirements. The mapping
 * from byte range to underlying hardware protection configuration is
 * handled internally by the platform.
 *
 * @param start  Inclusive start offset in bytes.
 * @param end    Exclusive end offset in bytes.
 *
 * @return true on successful configuration of the protection range,
 *         false on failure.
 */
bool flash_em32_write_protect_1_range(uint32_t start, uint32_t end);

/**
 * @brief Enable the second write-protection range.
 *
 * This function enables an additional flash write-protection range covering
 * the byte region [start, end).
 *
 * The second protection range is typically used to extend protection beyond
 * the region covered by the first range.
 *
 * The input range must be aligned to hardware requirements. The mapping
 * from byte range to underlying hardware protection configuration is
 * handled internally by the platform.
 *
 * @param start  Inclusive start offset in bytes.
 * @param end    Exclusive end offset in bytes.
 *
 * @return true on successful configuration of the protection range,
 *         false on failure.
 */
bool flash_em32_write_protect_2_range(uint32_t start, uint32_t end);

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
