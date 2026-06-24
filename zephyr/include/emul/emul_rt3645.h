/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Emulator for Richtek RT3645 IMVP9.1 PWM Controller
 */

#ifndef EMUL_RT3645_H
#define EMUL_RT3645_H

#include <zephyr/drivers/emul.h>
#include <zephyr/sys/slist.h>

/**
 * @brief Read register byte from rt3645 emulator
 *
 * @param emul Pointer to I2C rt3645 emulator
 * @param reg Address of register
 * @param val Pointer where byte to read should be stored
 *
 * @return 0 on success
 * @return -EINVAL when register is out of range defined in rt3645 private
 *                 register or val is NULL
 */
int rt3645_emul_read_reg(const struct emul *emul, int reg, uint8_t *val);

/**
 * @brief Resetting each byte of registers from rt3645 emulator
 *
 * @param emul Pointer to I2C rt3645 emulator
 *
 */
void rt3645_emul_reset_regs(const struct emul *emul);

/**
 * @brief Set rt3645 emulator in config mode. Emulator needs to be in config
 *        mode in order to modify paged registers.
 *
 * @param emul Pointer to I2C rt3645 emulator
 *
 * @return true If emulator is in config mode, false otherwise.
 */
bool rt3645_emul_in_config_mode(const struct emul *emul);

/**
 * @brief Set the NVM status register value in the rt3645 emulator.
 *        Used by tests to simulate NVM programming/reload success or failures.
 *
 * @param emul Pointer to I2C rt3645 emulator
 * @param stat The status value to set (e.g. 0xE0 for success, 0xA0 for prog
 * fail)
 */
void rt3645_emul_set_nvm_stat(const struct emul *emul, uint8_t stat);

/**
 * @brief Set the product ID register value in the rt3645 emulator.
 *        Used by tests to simulate incorrect product ID failures.
 *
 * @param emul Pointer to I2C rt3645 emulator
 * @param id The product ID value to set (expected is 0x45)
 */
void rt3645_emul_set_product_id(const struct emul *emul, uint8_t id);

#endif /* EMUL_RT3645_H */
