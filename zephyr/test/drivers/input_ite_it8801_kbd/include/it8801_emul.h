/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef IT8801_EMUL_H
#define IT8801_EMUL_H

#include "emul/emul_common_i2c.h"

#include <stdint.h>

/**
 * @brief IT8801 emulator data structure
 *
 * Contains the I2C common emulator data and a register array
 * to simulate the IT8801 hardware registers.
 */
struct it8801_emul_data {
	struct i2c_common_emul_data common;
	uint8_t regs[256];
};

/**
 * @brief Reset the IT8801 emulator to default state
 *
 * Clears all registers to 0x00, simulating a hardware reset.
 *
 * @param emul Pointer to the emulator instance
 */
void it8801_emul_reset(const struct emul *emul);

/**
 * @brief Get the value of a specific register
 *
 * @param emul Pointer to the emulator instance
 * @param reg Register address to read (0x00-0xFF)
 * @return Current value of the register
 */
uint8_t it8801_emul_get_reg(const struct emul *emul, uint8_t reg);

/**
 * @brief Set the value of a specific register
 *
 * Used by tests to simulate hardware state changes.
 *
 * @param emul Pointer to the emulator instance
 * @param reg Register address to write (0x00-0xFF)
 * @param val Value to write to the register
 */
void it8801_emul_set_reg(const struct emul *emul, uint8_t reg, uint8_t val);

/**
 * @brief Get the I2C common emulator data
 *
 * Used to configure I2C failure injection for testing error paths.
 *
 * @param emul Pointer to the emulator instance
 * @return Pointer to I2C common emulator data
 */
struct i2c_common_emul_data *it8801_emul_get_i2c_data(const struct emul *emul);

#endif /* IT8801_EMUL_H */
