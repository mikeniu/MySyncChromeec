/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "driver/accel_bma5xy.h"
#include "emul/emul_bma5xy.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "i2c.h"
#include "motion_sense.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/math_extras.h>
#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>
#endif

#define DT_DRV_COMPAT cros_bma5xy_emul

LOG_MODULE_REGISTER(bma5xy_emul, LOG_LEVEL_INF);

struct bma5xy_emul_data {
	struct i2c_common_emul_data i2c;
	/** True if the sensor is currently enabled. */
	bool accel_enabled;
	/** Current sensor range, ±2/4/8/16g; value is positive gs. */
	uint8_t accel_range;
	/** Raw register value of ACC_CONF:acc_odr. */
	uint8_t odr_raw;
	/** Current sensor reading on XYZ axes, in milli-g. */
	intv3_t acceleration;
	/** Axis offset register values, XYZ. */
	uint16_t offset[3];
	/**
	 * True if the sensor FIFO is currently enabled.
	 *
	 * Only headerless mode for the accelerometer alone is supported.
	 */
	bool fifo_enabled;
	/** Pointer to data that will be read from the FIFO. */
	const uint8_t *fifo_data;
	/** Number of bytes remaining in fifo_data. */
	uint16_t fifo_available;

	/** True if in latched interrupt mode, otherwise non-latched. */
	bool interrupt_mode_latched;
	/** Raw value of INT1_IO_CTRL register. */
	uint8_t int1_io_ctrl;
	/** Raw value of INT_MAP_DATA register. */
	uint8_t int_map_data;
};

struct bma5xy_emul_cfg {
	struct i2c_common_emul_cfg i2c;
	int sensor_id;
};

void bma5xy_emul_reset(const struct emul *emul)
{
	struct bma5xy_emul_data *data = emul->data;

	i2c_common_emul_set_read_fail_reg(&data->i2c,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_func(&data->i2c, NULL, NULL);
	i2c_common_emul_set_write_fail_reg(&data->i2c,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_func(&data->i2c, NULL, NULL);

	data->accel_enabled = false;
	data->accel_range = 2;
	data->odr_raw = 8;
	memset(data->acceleration, 0, sizeof(data->acceleration));
	memset(data->offset, 0, sizeof(data->offset));
	data->nv_config = 0;
	data->fifo_enabled = false;
	data->fifo_data = NULL;
	data->fifo_available = 0;
	data->interrupt_mode_latched = false;
}

struct i2c_common_emul_data *bma5xy_emul_get_i2c(const struct emul *emul)
{
	struct bma5xy_emul_data *data = emul->data;

	return &data->i2c;
}

struct motion_sensor_t *bma5xy_emul_get_sensor_data(const struct emul *emul)
{
	return &motion_sensors[bma5xy_emul_get_sensor_num(emul)];
}

int bma5xy_emul_get_sensor_num(const struct emul *emul)
{
	const struct bma5xy_emul_cfg *cfg = emul->cfg;

	return cfg->sensor_id;
}

bool bma5xy_emul_is_accel_enabled(const struct emul *emul)
{
	struct bma5xy_emul_data *data = emul->data;
	return data->accel_enabled;
}

void bma5xy_emul_set_accel_enabled(const struct emul *emul, bool enabled)
{
	struct bma5xy_emul_data *data = emul->data;

	data->accel_enabled = enabled;
}

uint8_t bma5xy_emul_get_accel_range(const struct emul *emul)
{
	struct bma5xy_emul_data *data = emul->data;

	return data->accel_range;
}

uint32_t bma5xy_emul_get_odr(const struct emul *emul)
{
	struct bma5xy_emul_data *data = emul->data;

	/*
	 * This function deliberately differs from bma5_reg_to_odr() to provide
	 * an obviously-correct reference for tests.
	 */
	switch (data->odr_raw) {
	case 1:
		return 3125;
	case 2:
		return 6250;
	case 3:
		return 12500;
	case 4:
		return 25000;
	case 5:
		return 50000;
	case 6:
		return 100000;
	case 7:
		return 200000;
	case 8:
		return 400000;
	case 9:
		return 800000;
	case 10:
		return 1600000;
	default:
		LOG_ERR("ODR register value %#x is reserved", data->odr_raw);
		return 0;
	}
}

void bma5xy_emul_set_accel_data(const struct emul *emul, int x, int y, int z)
{
	struct bma5xy_emul_data *data = emul->data;

	data->acceleration[0] = x;
	data->acceleration[1] = y;
	data->acceleration[2] = z;
}

void bma5xy_emul_get_offset(const struct emul *emul, int8_t (*offset)[3])
{
	struct bma5xy_emul_data *data = emul->data;

	memcpy(offset, data->offset, sizeof(*offset));
}

uint8_t bma5xy_emul_get_nv_conf(const struct emul *emul)
{
	struct bma5xy_emul_data *data = emul->data;

	return data->nv_config;
}

bool bma5xy_emul_is_fifo_enabled(const struct emul *emul)
{
	struct bma5xy_emul_data *data = emul->data;

	return data->fifo_enabled;
}

void bma5xy_emul_set_fifo_data(const struct emul *emul,
			       const uint8_t *fifo_data, uint16_t data_sz)
{
	struct bma5xy_emul_data *data = emul->data;

	__ASSERT(data_sz % 6 == 0,
		 "FIFO data should be an integer number of frames");
	data->fifo_data = fifo_data;
	data->fifo_available = data_sz;
}

uint8_t bma5xy_emul_get_interrupt_config(const struct emul *emul,
					 bool *latched_mode)
{
	struct bma5xy_emul_data *data = emul->data;

	*latched_mode = data->interrupt_mode_latched;
	return data->int_map_data;
}

static int bma5xy_emul_read_byte(const struct emul *target, int reg,
				 uint8_t *val, int bytes)
{
	struct bma5xy_emul_data *data = target->data;

	if (reg != BMA5_FIFO_DATA_ADDR) {
		/*
		 * Burst reads autoincrement register addresses, except for
		 * FIFO data which reads from the FIFO instead.
		 */
		reg += bytes;
	}

	switch (reg) {
	case BMA5_CHIP_ID_ADDR:
		*val = 0xC2; /* BMA530_CHIP_ID */
		return 0;
	case BMA5_DATA_0_ADDR: /* ACC_X(LSB) */
		*val = (data->acceleration[0] & GENMASK(7, 0)) << 8;
		return 0;
	case BMA5_DATA_0_ADDR + 1: /* ACC_X(MSB) */
		*val = (data->acceleration[0] >> 8) & GENMASK(7, 0);
		return 0;
	case BMA5_DATA_0_ADDR + 2: /* ACC_Y(LSB) */
		*val = (data->acceleration[1] & GENMASK(7, 0)) << 8;
		return 0;
	case BMA5_DATA_0_ADDR + 3: /* ACC_Y(MSB) */
		*val = (data->acceleration[1] >> 8) & GENMASK(7, 0);
		return 0;
	case BMA5_DATA_0_ADDR + 4: /* ACC_Z(LSB) */
		*val = (data->acceleration[2] & GENMASK(7, 0)) << 8;
		return 0;
	case BMA5_DATA_0_ADDR + 5: /* ACC_Z(MSB) */
		*val = (data->acceleration[2] >> 8) & GENMASK(7, 0);
		return 0;
	case BMA5_INT1_STATUS_0:
		*val = data->fifo_available > 0 ? 0x01 : 0; /* acc_drdy_int */
		return 0;
	case BMA5_FIFO_LENGTH_0_ADDR: /* LSB */
		*val = data->fifo_available & 0xFF;
		return 0;
	case BMA5_FIFO_LENGTH_0_ADDR + 1: /* MSB */
		*val = data->fifo_available >> 8;
		return 0;
	case BMA5_FIFO_DATA_ADDR:
		/*
		 * Read FIFO data; only supporting headerless mode with accel
		 * data only.
		 *
		 * Partial reads (of less than an entire frame) do not consume
		 * FIFO data, so we track the amount read in this burst. Reading
		 * past the end of the FIFO returns 0x8000.
		 */
		if (bytes % 6 >= data->fifo_available) {
			/* Out of data. */
			if (bytes % 2 == 0) {
				*val = 0;
			} else {
				*val = 0x80;
			}
			return 0;
		}
		*val = data->fifo_data[bytes % 6];
		if (bytes % 6 == 5) {
			/* Consume frame after reading the entire thing. */
			data->fifo_data += 6;
			data->fifo_available -= 6;
		}
		return 0;
	case BMA5_ACC_CONF0_ADDR:
		*val = data->accel_enabled ? BMA5_ENABLE : BMA5_DISABLE;
		return 0;
	case BMA5_STATUS_ADDR:
		return 0;
	case BMA5_ACCEL_CONF1_ADDR:
		*val = data->odr_raw | BMA5_ACCEL_ODR_MSK;
		return 0;
	case BMA5_ACCEL_CONF2_ADDR:
		*val = u32_count_trailing_zeros(data->accel_range) - 1;
		__ASSERT_NO_MSG(*val >= 0 && *val <= 3);
		return 0;
	case BMA5_OFFSET_0_ADDR:
	case BMA5_OFFSET_0_ADDR + 1:
	case BMA5_OFFSET_2_ADDR:
	case BMA5_OFFSET_2_ADDR + 1:
	case BMA5_OFFSET_4_ADDR:
	case BMA5_OFFSET_4_ADDR + 1:
		*val = data->offset[reg - BMA5_OFFSET_0_ADDR];
		return 0;
	}

	LOG_WRN("unhandled I2C read from register %#x", reg);
	return -ENOTSUP;
}

static int bma5xy_emul_write_byte(const struct emul *target, int reg,
				  uint8_t val, int bytes)
{
	struct bma5xy_emul_data *data = target->data;

	if (bytes != 1) {
		LOG_ERR("multi-byte writes are not supported");
		return -ENOTSUP;
	}

	switch (reg) {
	case BMA5_ACC_CONF0_ADDR:
		if (val != BMA5_ENABLE && val != BMA5_DISABLE) {
			LOG_ERR("invalid ACC_CONF0 write: %#x", val);
			return -EINVAL;
		}
		data->accel_enabled = (val == BMA5_ENABLE);
		return 0;	
	case BMA5_ACCEL_CONF1_ADDR:
		data->odr_raw = val & BMA5_ACCEL_ODR_MSK;
		return 0;
	case BMA5_ACCEL_CONF2_ADDR:
		if ((val & GENMASK(1, 0)) != val) {
			LOG_ERR("reserved bits set in ACC_RANGE write: %#x",
				val);
			return -EINVAL;
		}
		/* 0 => 2, 1 => 4, ... 3 => 16 */
		data->accel_range = 2 << val;
		return 0;
	case BMA5_FIFO_CONFIG_0_ADDR:
		if (val & ~BMA5_FIFO_ACC_EN) {
			LOG_ERR("unsupported bits set in FIFO_CONFIG_0"
				" write: %#x",
				val);
			return -EINVAL;
		}
		data->fifo_enabled = (val & BMA5_FIFO_ACC_EN) != 0;
		return 0;
	case BMA5_INT1_CONF_ADDR:
		data->interrupt_mode_latched = (val & 0x01) != 0;
		return 0;
	case BMA5_INT_MAP_DATA_ADDR:
		data->int_map_data = val;
		return 0;
	case BMA5_FIFO_CTRL_ADDR:
		if (val != BMA5_FIFO_RST) {
			LOG_ERR("unsupported FIFO_CTRL write: %#x", val);
			return -EINVAL;
		}
	case BMA5_FIFO_WM0:
	case BMA5_FIFO_WM1:
		/* Ignore watermark writes for now. */
		return 0;
	case BMA5_INT1_STATUS_0:
		return 0;
	/* Offset registers */
	case BMA5_OFFSET_0_ADDR:
	case BMA5_OFFSET_0_ADDR + 1:
	case BMA5_OFFSET_2_ADDR:
	case BMA5_OFFSET_2_ADDR + 1:
	case BMA5_OFFSET_4_ADDR:
	case BMA5_OFFSET_4_ADDR + 1:
		data->offset[reg - BMA5_OFFSET_0_ADDR] = val;
		return 0;
	}

	LOG_WRN("unhandled I2C write to register %#x", reg);
	return -ENOTSUP;
}

static int bma5xy_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	struct bma5xy_emul_data *data = emul->data;

	data->i2c.i2c = parent;
	i2c_common_emul_init(&data->i2c);
	bma5xy_emul_reset(emul);

	return 0;
}

#define INIT_BMA5XY(n)                                                    \
	static struct bma5xy_emul_data bma5xy_emul_data_##n = {         \
		.i2c = {                                                \
			.write_byte = bma5xy_emul_write_byte,           \
			.read_byte = bma5xy_emul_read_byte,             \
			.i2c = DEVICE_DT_GET(DT_INST_PARENT(n)),        \
		},                                                      \
	}; \
	static const struct bma5xy_emul_cfg bma5xy_emul_cfg_##n = {	\
		.i2c = {                                                \
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
			.addr = DT_INST_REG_ADDR(n),                    \
		},						        \
		.sensor_id = SENSOR_ID(DT_INST_PHANDLE(n,               \
						       motionsense_sensor)), \
	};     \
	EMUL_DT_INST_DEFINE(n, bma5xy_emul_init, &bma5xy_emul_data_##n,   \
			    &bma5xy_emul_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_BMA5XY)
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

#ifdef CONFIG_ZTEST
static void bma5xy_emul_reset_rule_before(const struct ztest_unit_test *test,
					  void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

#define BMA5XY_EMUL_RESET_RULE_BEFORE(n) \
	bma5xy_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)))

	DT_INST_FOREACH_STATUS_OKAY(BMA5XY_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(bma5xy_emul_reset, bma5xy_emul_reset_rule_before, NULL);
#endif /* CONFIG_ZTEST */
