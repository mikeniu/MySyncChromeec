/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_LIS2DH_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_LIS2DH_H_

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>

/**
 * @brief Reset the state of the lis2dh emulator.
 *
 * @param emul The emulator to reset.
 */
void lis2dh_emul_reset(const struct emul *emul);

int lis2dh_emul_get_interrupt_pin(const struct emul *emul);

/** Get the I2C interface emulator used by this emulator. */
struct i2c_common_emul_data *lis2dh_emul_get_i2c(const struct emul *emul);

/**
 * @brief Set the who-am-i register value.
 *
 * By default the who-am-i register holds LIS2DH_WHO_AM_I, this function
 * enables overriding that value in order to drive testing.
 *
 * @param emul The emulator to modify.
 * @param who_am_i The new who-am-i register value.
 */
void lis2dh_emul_set_who_am_i(const struct emul *emul, uint8_t who_am_i);

/**
 * @brief Peeks at the value of a register without doing any I2C transaction.
 *        If the register is unsupported, or `emul` is NULL, this function
 *        asserts.
 *
 * @param emul The emulator to query
 * @param reg The register to access
 * @return The value of the register
 */
uint8_t lis2dh_emul_peek_reg(const struct emul *emul, int reg);

/**
 * @brief Retrieves the ODR[3:0] bits from CRTL1 register
 *
 * @param emul The emulator to query
 * @return The ODR bits, right-aligned
 */
uint8_t lis2dh_emul_peek_odr(const struct emul *emul);

/**
 * @brief Check if fifo mode is enabled
 *
 * @param emul The emulator to query
 * @return 1 if fifo mode is enabled, 0 otherwise.
 */
int lis2dh_emul_is_fifo_enabled(const struct emul *emul);

/**
 * @brief Updates the current 3-axis acceleromter reading and
 *        sets the DRDY (data ready) flag.
 * @param emul Reference to current LIS2DH emulator.
 * @param reading array of int X, Y, and Z readings.
 * @return 0 on success, or -EINVAL if readings are out of bounds.
 */
int lis2dh_emul_set_accel_reading(const struct emul *emul, intv3_t reading);

/**
 * @brief Clears the current accelerometer reading and resets the
 *        DRDY (data ready) flag.
 * @param emul Reference to current LIS2DH emulator.
 */
void lis2dh_emul_clear_accel_reading(const struct emul *emul);

/**
 * @brief Set data for the fifo which is managed as a pointer and size of bytes
 * @param emul Reference to current LIS2DH emulator.
 * @param pointer to fifo data that's being added
 * @param number of bytes being added to the fifo
 */
void lis2dh_emul_set_fifo_data(const struct emul *emul,
			       const int16_t *fifo_data, uint8_t data_sz);

/**
 * @brief Returns pointer to i2c_common_emul_data for argument emul
 *
 * @param emul Pointer to LIS2DH emulator
 * @return Pointer to i2c_common_emul_data from argument emul
 */
struct i2c_common_emul_data *
emul_lis2dh_get_i2c_common_data(const struct emul *emul);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_LIS2DH_H_ */
