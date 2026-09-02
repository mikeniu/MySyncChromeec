/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_FPC1025_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_FPC1025_H_

/* FPC1025 example hardware id */
#define FPC1025_HWID 0x021F

/**
 * Set hardware id returned by emulator
 *
 * @param target The target emulator to modify
 * @param hardware_id new hardware id
 */
void fpc1025_set_hwid(const struct emul *target, uint16_t hardware_id);

/**
 * Get low power mode status
 *
 * @param target The target emulator to get status
 */
uint8_t fpc1025_get_low_power_mode(const struct emul *target);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_FPC1025_H_ */
