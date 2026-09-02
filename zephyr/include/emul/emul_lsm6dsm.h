/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_LSM6DSM_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_LSM6DSM_H_

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
void emul_lsm6dsm_append_sample(const struct emul *target,
				enum motionsensor_type type, float x, float y,
				float z);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_LSM6DSM_H_ */
