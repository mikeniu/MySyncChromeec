/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accel_lis2dh.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_lis2dh.h"
#include "i2c.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/emul_stub_device.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#define DT_DRV_COMPAT cros_lis2dh

LOG_MODULE_REGISTER(lis2dh_emul, LOG_LEVEL_DBG);

struct lis2dh_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;
	/** Emulated who-am-i register */
	uint8_t who_am_i_reg;
	/** Emulated ctrl1 register */
	uint8_t ctrl1_reg;
	/** Emulated ctrl2 register */
	uint8_t ctrl2_reg;
	/** Emulated ctrl3 register */
	uint8_t ctrl3_reg;
	/** Emulated ctrl4 register */
	uint8_t ctrl4_reg;
	/** Emulated ctrl5 register */
	uint8_t ctrl5_reg;
	/** Emulated ctrl6 register */
	uint8_t ctrl6_reg;
	/** Emulated status register */
	uint8_t status_reg;
	/** Current X, Y, and Z output data registers */
	int16_t accel_data[3];
	/** FIFO control register */
	uint8_t fifo_ctrl_reg;
	/** FIFO SRC register */
	uint8_t fifo_src_reg;
	/** Pointer to data that will be read from the FIFO. */
	const int16_t *fifo_data;
	/** Number of bytes remaining in fifo_data. */
	uint8_t fifo_available;
};

struct lis2dh_emul_cfg {
	/** Common I2C config */
	struct i2c_common_emul_cfg common;
	const struct gpio_dt_spec gpio_spec;
};

static void lis2dh_emul_set_interrupt_pin(const struct emul *emul, bool active)
{
	const struct lis2dh_emul_cfg *config = emul->cfg;

	if (config->gpio_spec.port == NULL) {
		return;
	}

	LOG_INF("setting interrupt gpio %s", active ? "true" : "false");
	/* Assuming active low */
	gpio_emul_input_set(config->gpio_spec.port, config->gpio_spec.pin,
			    !active);
}

int lis2dh_emul_get_interrupt_pin(const struct emul *emul)
{
	__ASSERT(emul, "emul is NULL");

	const struct lis2dh_emul_cfg *config = emul->cfg;

	return gpio_pin_get_dt(&config->gpio_spec);
}

void lis2dh_emul_reset(const struct emul *emul)
{
	struct lis2dh_emul_data *data = emul->data;

	i2c_common_emul_set_read_fail_reg(&data->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(&data->common,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_func(&data->common, NULL, NULL);
	i2c_common_emul_set_write_func(&data->common, NULL, NULL);
	data->who_am_i_reg = LIS2DH_WHO_AM_I;
	data->ctrl1_reg = 0;
	data->ctrl2_reg = 0;
	data->ctrl3_reg = 0;
	data->ctrl4_reg = 0;
	data->ctrl5_reg = 0;
	data->ctrl6_reg = 0;
	data->status_reg = 0;
	data->fifo_ctrl_reg = 0;
	data->fifo_src_reg = 0;

	data->fifo_data = 0;
	data->fifo_available = 0;

	memset(data->accel_data, 0, sizeof(data->accel_data));
	lis2dh_emul_set_interrupt_pin(emul, false);
}

struct i2c_common_emul_data *lis2dh_emul_get_i2c(const struct emul *emul)
{
	struct lis2dh_emul_data *data = emul->data;

	return &data->common;
}

void lis2dh_emul_set_who_am_i(const struct emul *emul, uint8_t who_am_i)
{
	struct lis2dh_emul_data *data = emul->data;

	data->who_am_i_reg = who_am_i;
}

static int lis2dh_emul_read_byte(const struct emul *emul, int reg, uint8_t *val,
				 int bytes)
{
	struct lis2dh_emul_data *data = emul->data;
	int n_samples;

	switch (reg) {
	case 0x00:
		/*
		 * Handle low power wake up case that can happen with suspend
		 * resume if one of the lis2dh tests includes k_msleep().
		 */
		if (bytes == 0) {
			return 0;
		}
		break;
	case LIS2DH_WHO_AM_I_REG:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->who_am_i_reg;
		break;
	case LIS2DH_CTRL1_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl1_reg;
		break;
	case LIS2DH_CTRL2_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl2_reg;
		break;
	case LIS2DH_CTRL3_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl3_reg;
		break;
	case LIS2DH_CTRL4_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl4_reg;
		break;
	case LIS2DH_CTRL5_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl5_reg;
		break;
	case LIS2DH_CTRL6_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl6_reg;
		break;
	case LIS2DH_STATUS_REG:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->status_reg;
		break;
	case LIS2DH_OUT_X_L_ADDR:
	case LIS2DH_OUT_X_H_ADDR:
	case LIS2DH_OUT_Y_L_ADDR:
	case LIS2DH_OUT_Y_H_ADDR:
	case LIS2DH_OUT_Z_L_ADDR:
	case LIS2DH_OUT_Z_H_ADDR:
		/* Allow multi-byte reads within this range of
		 * registers. `bytes` is actually an offset past the
		 * starting register `reg`.
		 */

		__ASSERT_NO_MSG(LIS2DH_OUT_X_L_ADDR + bytes <=
				LIS2DH_OUT_Z_H_ADDR);

		/* 0 is OUT_X_L_ADDR .. 5 is OUT_Z_H_ADDR */
		int offset_into_odrs = reg - LIS2DH_OUT_X_L_ADDR + bytes;

		/* Which of the 3 channels we're reading. 0 = X, 1 = Y,
		 * 2 = Z */
		int channel = offset_into_odrs / 2;

		if (offset_into_odrs % 2 == 0) {
			/* Get the LSB (L reg) */
			*val = data->accel_data[channel] & 0xFF;
		} else {
			/* Get the MSB (H reg) */
			*val = (data->accel_data[channel] >> 8) & 0xFF;
		}
		break;
	case LIS2DH_FIFO_CTRL_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->fifo_ctrl_reg;
		break;
	case LIS2DH_FIFO_SRC_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->fifo_src_reg;
		break;
	case (0x80 | LIS2DH_OUT_X_L_ADDR):
		/*
		 * Fifo mode is enabled, so reads from
		 * LIS2DH_OUT_X_L_ADDR need to emulate fifo
		 */
		__ASSERT(lis2dh_emul_is_fifo_enabled(emul),
			 "Fifo mode is not enabled");

		/* Ensure bytes to be read <= bytes available */
		__ASSERT(data->fifo_available > 0,
			 "Attempting to read from empty fifo");

		if ((bytes % 2) == 0) {
			*val = data->fifo_data[0] & 0xff;
		} else {
			*val = data->fifo_data[0] >> 8;
		}
		data->fifo_available--;

		/*
		 * Update fifo pointer when a full X, Y, or Z sample has been
		 * read */
		if (bytes % 2 == 1) {
			data->fifo_data++;
		}

		if (bytes % OUT_XYZ_SIZE == 5) {
			int wtm_level;

			/*
			 * when bytes == 5, then a full sample reading is
			 * completed and the fss field needs to be decremented.
			 */
			n_samples = data->fifo_src_reg & LIS2DH_FIFO_DIFF_MASK;
			__ASSERT(n_samples > 0, "fifo samples should be > 0");
			data->fifo_src_reg &= ~LIS2DH_FIFO_DIFF_MASK;
			data->fifo_src_reg |= --n_samples;

			/* if n_samples < wtm,, then clear interrupt */
			wtm_level = data->fifo_ctrl_reg &
				    LIS2DH_FIFO_THRESHOLD_MASK;
			if (n_samples < wtm_level) {
				lis2dh_emul_set_interrupt_pin(emul, false);
			}
		}
		break;
	default:
		__ASSERT(false, "No read handler for register 0x%02x", reg);
		return -EINVAL;
	}
	return 0;
}

uint8_t lis2dh_emul_peek_reg(const struct emul *emul, int reg)
{
	__ASSERT(emul, "emul is NULL");

	uint8_t val;
	int rv;

	rv = lis2dh_emul_read_byte(emul, reg, &val, 0);
	__ASSERT(rv == 0, "Read function returned non-zero: %d", rv);

	return val;
}

uint8_t lis2dh_emul_peek_odr(const struct emul *emul)
{
	__ASSERT(emul, "emul is NULL");

	uint8_t reg = lis2dh_emul_peek_reg(emul, LIS2DH_ACC_ODR_ADDR);

	return (reg & LIS2DH_ACC_ODR_MASK) >>
	       __builtin_ctz(LIS2DH_ACC_ODR_MASK);
}

int lis2dh_emul_is_fifo_enabled(const struct emul *emul)
{
	struct lis2dh_emul_data *data;

	__ASSERT(emul, "emul is NULL");

	data = emul->data;

	return ((data->fifo_ctrl_reg & LIS2DH_FIFO_MODE_MASK) !=
		LIS2DH_FIFO_BYPASS_MODE);
}

static int lis2dh_emul_write_byte(const struct emul *emul, int reg, uint8_t val,
				  int bytes)
{
	struct lis2dh_emul_data *data = emul->data;

	switch (reg) {
	case LIS2DH_WHO_AM_I_REG:
		LOG_ERR("Can't write to who-am-i register");
		return -EINVAL;
	case LIS2DH_CTRL1_ADDR:
		data->ctrl1_reg = val;
		break;
	case LIS2DH_CTRL2_ADDR:
		data->ctrl2_reg = val;
		break;
	case LIS2DH_CTRL3_ADDR:
		data->ctrl3_reg = val;
		break;
	case LIS2DH_CTRL4_ADDR:
		data->ctrl4_reg = val;
		break;
	case LIS2DH_CTRL5_ADDR:
		data->ctrl5_reg = val;
		break;
	case LIS2DH_CTRL6_ADDR:
		data->ctrl6_reg = val;
		break;
	case LIS2DH_STATUS_REG:
		__ASSERT(false,
			 "Attempt to write to read-only status register");
		return -EINVAL;
	case LIS2DH_OUT_X_L_ADDR:
	case LIS2DH_OUT_X_H_ADDR:
	case LIS2DH_OUT_Y_L_ADDR:
	case LIS2DH_OUT_Y_H_ADDR:
	case LIS2DH_OUT_Z_L_ADDR:
	case LIS2DH_OUT_Z_H_ADDR:
		__ASSERT(false,
			 "Attempt to write to data output register 0x%02x",
			 reg);
		return -EINVAL;
	case LIS2DH_FIFO_CTRL_ADDR:
		data->fifo_ctrl_reg = val;
		break;
	case LIS2DH_FIFO_SRC_ADDR:
		data->fifo_src_reg = val;
		break;
	default:
		__ASSERT(false, "No write handler for register 0x%02x", reg);
		return -EINVAL;
	}
	return 0;
}

static int emul_lis2dh_init(const struct emul *emul,
			    const struct device *parent)
{
	struct lis2dh_emul_data *data = emul->data;

	data->common.i2c = parent;
	i2c_common_emul_init(&data->common);
	lis2dh_emul_reset(emul);

	return 0;
}

int lis2dh_emul_set_accel_reading(const struct emul *emul, intv3_t reading)
{
	__ASSERT(emul, "emul is NULL");
	struct lis2dh_emul_data *data = emul->data;

	for (int i = X; i <= Z; i++) {
		/* Ensure we fit in a 12-bit signed integer */
		if (reading[i] < LIS2DH_SAMPLE_MIN ||
		    reading[i] > LIS2DH_SAMPLE_MAX) {
			return -EINVAL;
		}
		/* Readings are left-aligned, so shift over by 2 */
		data->accel_data[i] = reading[i] * 4;
	}

	/* Set the DRDY (data ready) bit */
	data->status_reg |= LIS2DH_STS_XLDA_UP;

	return 0;
}

void lis2dh_emul_clear_accel_reading(const struct emul *emul)
{
	__ASSERT(emul, "emul is NULL");
	struct lis2dh_emul_data *data = emul->data;

	/* Zero out the registers and reset DRDY bit */
	memset(data->accel_data, 0, sizeof(data->accel_data));
	data->status_reg &= ~LIS2DH_STS_XLDA_UP;
}

void lis2dh_emul_set_fifo_data(const struct emul *emul,
			       const int16_t *fifo_data, uint8_t data_sz)
{
	struct lis2dh_emul_data *data = emul->data;
	int n_samples;
	int wtm_level;
	int wtm_int_enable;

	__ASSERT(data_sz % 6 == 0,
		 "FIFO data should be an integer number of frames");

	data->fifo_data = fifo_data;
	data->fifo_available = data_sz;

	/*
	 * Adjust fifo fss field by number of bytes added. Note that the fss
	 * field represents number of samples where each sample is a 3 axis 16
	 * bit reading.
	 */
	n_samples = data->fifo_src_reg & LIS2DH_FIFO_DIFF_MASK;
	n_samples += data_sz / OUT_XYZ_SIZE;
	data->fifo_src_reg &= ~LIS2DH_FIFO_DIFF_MASK;
	data->fifo_src_reg |= n_samples;

	/* If n_smaples >= watermark, then set interrupt gpio active */
	wtm_level = data->fifo_ctrl_reg & LIS2DH_FIFO_THRESHOLD_MASK;
	wtm_int_enable = data->ctrl3_reg & LIS2DH_INT1_FTH_MASK;
	LOG_INF("wtm_level = %d, wtm_int_enable = %d", wtm_level,
		wtm_int_enable);
	if (wtm_int_enable && (n_samples > wtm_level)) {
		lis2dh_emul_set_interrupt_pin(emul, true);
	}
}

#define INIT_LIS2DH(n)                                                  \
	static struct lis2dh_emul_data lis2dh_emul_data_##n = {       \
		.common = {                                               \
			.write_byte = lis2dh_emul_write_byte,           \
			.read_byte = lis2dh_emul_read_byte,             \
		},                                                        \
	}; \
	static const struct lis2dh_emul_cfg lis2dh_emul_cfg_##n = {   \
		.common = {                                               \
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),   \
			.addr = DT_INST_REG_ADDR(n),                      \
		},                                                        \
		.gpio_spec = GPIO_DT_SPEC_INST_GET_OR(n, int_gpios, {}), \
	}; \
	EMUL_DT_INST_DEFINE(n, emul_lis2dh_init, &lis2dh_emul_data_##n, \
			    &lis2dh_emul_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_LIS2DH)
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

struct i2c_common_emul_data *
emul_lis2dh_get_i2c_common_data(const struct emul *emul)
{
	return emul->data;
}
