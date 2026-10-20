/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_LSM6DSO_H
#define __EMUL_LSM6DSO_H

#include "ec_commands.h"

#include <zephyr/drivers/emul.h>

/**
 * Append a sample to the emulator. If the FIFO is enabled, the sample will be
 * saved as both the next data and in the FIFO. Similarly, if the watermark is
 * set and interrupts are enabled, the interrupt signal will assert.
 *
 * @param target The emulated target
 * @param type The data type (accel/gyro)
 * @param x The x value of the data
 * @param y The y value of the data
 * @param z The z value of the data
 */
void emul_lsm6dso_append_sample(const struct emul *target,
				enum motionsensor_type type, float x, float y,
				float z);

/**
 * Append a raw FIFO sample with a specific tag to the emulator.
 *
 * @param target The emulated target
 * @param raw_tag The tag value (e.g. LSM6DSO_ACC_TAG, LSM6DSO_GYRO_TAG, or
 * invalid tag)
 * @param x The x axis raw value
 * @param y The y axis raw value
 * @param z The z axis raw value
 */
void emul_lsm6dso_append_raw_tag(const struct emul *target, uint8_t raw_tag,
				 int16_t x, int16_t y, int16_t z);

#endif /* __EMUL_LSM6DSO_H */
