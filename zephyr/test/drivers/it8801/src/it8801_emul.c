/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "it8801_emul.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul_stub_device.h>

#define DT_DRV_COMPAT ite_it8801_mfd

void it8801_emul_reset(const struct emul *emul)
{
	struct it8801_emul_data *data = (struct it8801_emul_data *)emul->data;

	memset(data->regs, 0, sizeof(data->regs));
}

uint8_t it8801_emul_get_reg(const struct emul *emul, uint8_t reg)
{
	struct it8801_emul_data *data = (struct it8801_emul_data *)emul->data;

	return data->regs[reg];
}

void it8801_emul_set_reg(const struct emul *emul, uint8_t reg, uint8_t val)
{
	struct it8801_emul_data *data = (struct it8801_emul_data *)emul->data;

	data->regs[reg] = val;
}

struct i2c_common_emul_data *it8801_emul_get_i2c_data(const struct emul *emul)
{
	struct it8801_emul_data *data = (struct it8801_emul_data *)emul->data;

	return &data->common;
}

/**
 * @brief I2C write handler for IT8801 emulator
 *
 * Simulates writing to IT8801 registers via I2C.
 *
 * @param emul Pointer to the emulator
 * @param reg Register address
 * @param val Value to write
 * @param bytes Byte count (used for multi-byte writes)
 * @param unused_data Unused parameter
 * @return 0 on success, -1 on invalid address
 */
static int it8801_emul_write(const struct emul *emul, int reg, uint8_t val,
			     int bytes, void *unused_data)
{
	struct it8801_emul_data *data = (struct it8801_emul_data *)emul->data;
	int pos = reg + bytes - 1;

	if (!IN_RANGE(pos, 0, 255)) {
		return -1;
	}
	data->regs[pos] = val;

	return 0;
}

/**
 * @brief I2C read handler for IT8801 emulator
 *
 * Simulates reading from IT8801 registers via I2C.
 *
 * @param emul Pointer to the emulator
 * @param reg Register address
 * @param val Pointer to store read value
 * @param bytes Byte count (used for multi-byte reads)
 * @param unused_data Unused parameter
 * @return 0 on success, -1 on invalid address
 */
static int it8801_emul_read(const struct emul *emul, int reg, uint8_t *val,
			    int bytes, void *unused_data)
{
	struct it8801_emul_data *data = (struct it8801_emul_data *)emul->data;
	int pos = reg + bytes;

	if (!IN_RANGE(pos, 0, 255)) {
		return -1;
	}
	*val = data->regs[pos];

	return 0;
}

/**
 * @brief Initialize IT8801 emulator
 *
 * Sets up the I2C common emulator functions and resets the device state.
 *
 * @param emul Pointer to the emulator
 * @param parent Parent device (I2C bus)
 * @return 0 on success
 */
static int it8801_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	struct it8801_emul_data *data = (struct it8801_emul_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, it8801_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, it8801_emul_write, NULL);
	it8801_emul_reset(emul);

	return 0;
}

#define INIT_IT8801_EMUL(n)                                             \
	static struct i2c_common_emul_cfg common_cfg_##n;               \
	static struct it8801_emul_data it8801_emul_data_##n;            \
	static struct i2c_common_emul_cfg common_cfg_##n = {            \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),         \
		.data = &it8801_emul_data_##n.common,                   \
		.addr = DT_INST_REG_ADDR(n)                             \
	};                                                              \
	static struct it8801_emul_data it8801_emul_data_##n = {         \
		.common = { .cfg = &common_cfg_##n }                    \
	};                                                              \
	EMUL_DT_INST_DEFINE(n, it8801_emul_init, &it8801_emul_data_##n, \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_IT8801_EMUL);

/* Create stub devices for the MFD parent nodes */
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
