/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_lsm6dso_emul

#include "driver/accelgyro_lsm6dso.h"
#include "driver/accelgyro_lsm6dso_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_lsm6dso.h"
#include "math_util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/emul_stub_device.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(lsm6dso_emul, CONFIG_SENSOR_LOG_LEVEL);

#define LSM6DSO_REG_COUNT 0x80
#define LSM6DSO_FIFO_MAX_SAMPLES 438
#define LSM6DSO_FIFO_BUFFER_SIZE \
	(LSM6DSO_FIFO_MAX_SAMPLES * LSM6DSO_FIFO_SAMPLE_SIZE)

struct lsm6dso_emul_data {
	struct i2c_common_emul_data i2c;
	uint8_t reg[LSM6DSO_REG_COUNT];
	/* FIFO is ~3kB (438 samples * 7 bytes = 3066 bytes) */
	uint8_t fifo[LSM6DSO_FIFO_BUFFER_SIZE];
	size_t fifo_head;
};

struct lsm6dso_emul_cfg {
	struct i2c_common_emul_cfg i2c;
	const struct gpio_dt_spec gpio_spec;
};

static int emul_lsm6dso_get_fifo_count(const struct emul *emul)
{
	struct lsm6dso_emul_data *data = emul->data;

	return ((int)FIELD_GET(LSM6DSO_FIFO_STS2_DIFF_MASK,
			       data->reg[LSM6DSO_FIFO_STS2_ADDR])
		<< LSM6DSO_FIFO_DIFF_MSB_SHIFT) |
	       FIELD_GET(LSM6DSO_FIFO_STS1_DIFF_MASK,
			 data->reg[LSM6DSO_FIFO_STS1_ADDR]);
}

static int emul_lsm6dso_get_fifo_wm(const struct emul *emul)
{
	struct lsm6dso_emul_data *data = emul->data;

	return ((int)FIELD_GET(LSM6DSO_FIFO_CTRL2_WTM_MASK,
			       data->reg[LSM6DSO_FIFO_CTRL2_ADDR])
		<< LSM6DSO_FIFO_DIFF_MSB_SHIFT) |
	       data->reg[LSM6DSO_FIFO_CTRL1_ADDR];
}

static void lsm6dso_emul_set_interrupt_pin(const struct emul *emul, bool active)
{
	const struct lsm6dso_emul_cfg *config = emul->cfg;
	struct lsm6dso_emul_data *data = emul->data;

	if (config->gpio_spec.port == NULL) {
		return;
	}

	bool int_enabled = (data->reg[LSM6DSO_INT1_CTRL] &
			    (LSM6DSO_INT_FIFO_TH | LSM6DSO_INT_FIFO_FULL |
			     LSM6DSO_INT_FIFO_OVR)) != 0;

	gpio_emul_input_set(config->gpio_spec.port, config->gpio_spec.pin,
			    !(active && int_enabled));
}

static void emul_lsm6dso_set_fifo_count(const struct emul *emul, int count)
{
	struct lsm6dso_emul_data *data = emul->data;
	int wm = emul_lsm6dso_get_fifo_wm(emul);
	bool watermark_reached = count >= wm && wm != 0;

	data->reg[LSM6DSO_FIFO_STS1_ADDR] = 0;
	data->reg[LSM6DSO_FIFO_STS2_ADDR] = 0;

	if (watermark_reached) {
		data->reg[LSM6DSO_FIFO_STS2_ADDR] |= LSM6DSO_FIFO_STS2_WTM;
	}
	if (count == 0) {
		data->reg[LSM6DSO_FIFO_STS2_ADDR] |= LSM6DSO_FIFO_STS2_EMPTY;
	} else {
		data->reg[LSM6DSO_FIFO_STS2_ADDR] |=
			FIELD_GET(LSM6DSO_FIFO_STS2_DIFF_MASK,
				  count >> LSM6DSO_FIFO_DIFF_MSB_SHIFT);
		data->reg[LSM6DSO_FIFO_STS1_ADDR] =
			FIELD_GET(LSM6DSO_FIFO_STS1_DIFF_MASK, count);
	}
	lsm6dso_emul_set_interrupt_pin(emul, watermark_reached);
	LOG_DBG("FIFO_STATUS (%d): 0x%02x %02x", count,
		data->reg[LSM6DSO_FIFO_STS1_ADDR],
		data->reg[LSM6DSO_FIFO_STS2_ADDR]);
}

void emul_lsm6dso_append_raw_tag(const struct emul *emul, uint8_t raw_tag,
				 int16_t x, int16_t y, int16_t z)
{
	struct lsm6dso_emul_data *data = emul->data;
	int current_fifo_count = emul_lsm6dso_get_fifo_count(emul);
	size_t fifo_pos;

	if (current_fifo_count >= LSM6DSO_FIFO_MAX_SAMPLES) {
		LOG_ERR("FIFO full");
		return;
	}

	fifo_pos = (data->fifo_head +
		    (size_t)current_fifo_count * LSM6DSO_FIFO_SAMPLE_SIZE) %
		   sizeof(data->fifo);

	data->fifo[fifo_pos++] = raw_tag << LSM6DSO_FIFO_TAG_SHIFT;
	sys_put_le16(x, &data->fifo[fifo_pos]);
	fifo_pos += sizeof(uint16_t);
	sys_put_le16(y, &data->fifo[fifo_pos]);
	fifo_pos += sizeof(uint16_t);
	sys_put_le16(z, &data->fifo[fifo_pos]);

	emul_lsm6dso_set_fifo_count(emul, current_fifo_count + 1);
}

void emul_lsm6dso_append_sample(const struct emul *emul,
				enum motionsensor_type type, float x, float y,
				float z)
{
	static const uint32_t la_fs[] = { 2, 16, 4, 8 };
	static const uint32_t g_fs[] = { 250, 500, 1000, 2000 };
	struct lsm6dso_emul_data *data = emul->data;
	int16_t reg_x, reg_y, reg_z;
	uint8_t out_reg;
	uint8_t raw_tag;
	uint8_t status_bit;
	uint32_t fs;

	__ASSERT_NO_MSG(type == MOTIONSENSE_TYPE_ACCEL ||
			type == MOTIONSENSE_TYPE_GYRO);

	if (type == MOTIONSENSE_TYPE_ACCEL) {
		fs = la_fs[FIELD_GET(LSM6DSO_ACCEL_FS_MASK,
				     data->reg[LSM6DSO_CTRL1_ADDR])];
		raw_tag = LSM6DSO_ACC_TAG;
		out_reg = LSM6DSO_ACCEL_OUT_X_L_ADDR;
		status_bit = LSM6DSO_STS_XLDA_UP;
	} else {
		fs = g_fs[FIELD_GET(LSM6DSO_GYRO_FS_MASK,
				    data->reg[LSM6DSO_CTRL2_ADDR])];
		raw_tag = LSM6DSO_GYRO_TAG;
		out_reg = LSM6DSO_GYRO_OUT_X_L_ADDR;
		status_bit = LSM6DSO_STS_GDA_UP;
	}

	__ASSERT_NO_MSG(ABS(x) < (float)fs);
	__ASSERT_NO_MSG(ABS(y) < (float)fs);
	__ASSERT_NO_MSG(ABS(z) < (float)fs);

	reg_x = (int16_t)((x / fs) * INT16_MAX);
	reg_y = (int16_t)((y / fs) * INT16_MAX);
	reg_z = (int16_t)((z / fs) * INT16_MAX);

	data->reg[LSM6DSO_STATUS_REG] |= status_bit;
	sys_put_le16(reg_x, &data->reg[out_reg]);
	sys_put_le16(reg_y, &data->reg[out_reg + sizeof(uint16_t)]);
	sys_put_le16(reg_z, &data->reg[out_reg + 2 * sizeof(uint16_t)]);

	const bool append_to_fifo = (data->reg[LSM6DSO_FIFO_CTRL4_ADDR] &
				     LSM6DSO_FIFO_MODE_MASK) != 0;

	if (append_to_fifo) {
		emul_lsm6dso_append_raw_tag(emul, raw_tag, reg_x, reg_y, reg_z);
	}
}

static void lsm6dso_emul_reset(const struct emul *emul)
{
	struct lsm6dso_emul_data *data = emul->data;

	memset(data->reg, 0, sizeof(data->reg));
	data->reg[LSM6DSO_WHO_AM_I_REG] = LSM6DSO_WHO_AM_I;
	data->reg[LSM6DSO_CTRL3_ADDR] = LSM6DSO_IF_INC;
	data->fifo_head = 0;
	emul_lsm6dso_set_fifo_count(emul, 0);
}

static int lsm6dso_emul_read_byte(const struct emul *emul, int reg,
				  uint8_t *val, int bytes)
{
	struct lsm6dso_emul_data *data = emul->data;

	LOG_DBG("Reading byte %d starting at register 0x%02x", bytes, reg);
	if (bytes < 0 || reg < 0) {
		return -ENOTSUP;
	}

	if (reg == LSM6DSO_FIFO_DATA_ADDR_TAG) {
		int fifo_count = emul_lsm6dso_get_fifo_count(emul);
		size_t sample_offset = bytes % LSM6DSO_FIFO_SAMPLE_SIZE;

		if (fifo_count == 0) {
			LOG_ERR("Tried to read FIFO, but no entries are there");
			return -EINVAL;
		}
		*val = data->fifo[data->fifo_head + sample_offset];
		if (sample_offset == (LSM6DSO_FIFO_SAMPLE_SIZE - 1)) {
			data->fifo_head =
				(data->fifo_head + LSM6DSO_FIFO_SAMPLE_SIZE) %
				sizeof(data->fifo);
			emul_lsm6dso_set_fifo_count(emul, fifo_count - 1);
		}
		return 0;
	}

	if (reg + bytes >= sizeof(data->reg)) {
		return -ENOTSUP;
	}

	*val = data->reg[reg + bytes];
	return 0;
}

static int lsm6dso_emul_write_byte(const struct emul *emul, int reg,
				   uint8_t val, int bytes)
{
	struct lsm6dso_emul_data *data = emul->data;
	/*
	 * In i2c_common_emul, bytes == 1 for the first data byte because
	 * byte 0 of the I2C write message was the register address.
	 */
	int reg_addr = reg + bytes - 1;

	LOG_DBG("Writing byte %d starting at register 0x%02x: 0x%02x",
		bytes - 1, reg, val);
	if (bytes < 1 || reg < 0 || reg_addr >= sizeof(data->reg)) {
		return -ENOTSUP;
	}

	data->reg[reg_addr] = val;

	if (reg_addr == LSM6DSO_CTRL3_ADDR && (val & LSM6DSO_SW_RESET) != 0) {
		lsm6dso_emul_reset(emul);
	}

	return 0;
}

static int lsm6dso_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	const struct lsm6dso_emul_cfg *config = emul->cfg;
	struct lsm6dso_emul_data *data = emul->data;

	ARG_UNUSED(parent);

	i2c_common_emul_init(&data->i2c);
	lsm6dso_emul_reset(emul);

	__ASSERT(config->gpio_spec.port == NULL ||
			 (config->gpio_spec.dt_flags & GPIO_ACTIVE_LOW) != 0,
		 "The lsm6dso driver only supports active low configuration");

	return 0;
}

#define LSM6DSO_EMUL(inst)                                                    \
	static struct lsm6dso_emul_data lsm6dso_emul_data_##inst = {        \
		.i2c = {                                                    \
			.write_byte = lsm6dso_emul_write_byte,              \
			.read_byte = lsm6dso_emul_read_byte,                \
			.i2c = DEVICE_DT_GET(DT_INST_PARENT(inst)),         \
		},                                                          \
	}; \
	const static struct lsm6dso_emul_cfg lsm6dso_emul_cfg_##inst = {    \
		.i2c = {                                                    \
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(inst)),  \
			.data = &lsm6dso_emul_data_##inst.i2c,              \
			.addr = DT_INST_REG_ADDR(inst),                     \
		},                                                          \
		.gpio_spec = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {}), \
	}; \
	EMUL_DT_INST_DEFINE(inst, lsm6dso_emul_init,                          \
			    &lsm6dso_emul_data_##inst,                        \
			    &lsm6dso_emul_cfg_##inst, &i2c_common_emul_api,   \
			    NULL);

DT_INST_FOREACH_STATUS_OKAY(LSM6DSO_EMUL)
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE)
