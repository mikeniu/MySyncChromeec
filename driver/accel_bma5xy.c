/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Bosch Accelerometer driver for Chrome EC
 *
 * Supported: BMA530
 */

#include "accel_bma5xy.h"
#include "accelgyro.h"
#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "spi.h"
#include "task.h"
#include "util.h"

#include <motion_sense_fifo.h>

#ifdef CONFIG_ACCEL_BMA5XY_INT_EVENT
#define BMA5XY_USE_INTERRUPTS
#endif

#define GOTO_ON_ERROR(label, expr)  \
	do {                        \
		ret = expr;         \
		if (ret)            \
			goto label; \
	} while (0)
/**
 * Read 8bit register from accelerometer.
 */
static inline int bma5_read8(const struct motion_sensor_t *s, const int reg,
			     int *data_ptr)
{
	return i2c_read8(s->port, s->i2c_spi_addr_flags, reg, data_ptr);
}

/**
 * Write 8bit register to accelerometer.
 */
static inline int bma5_write8(const struct motion_sensor_t *s, const int reg,
			      int data)
{
	int ret;

	ret = i2c_write8(s->port, s->i2c_spi_addr_flags, reg, data);

	/*
	 * From Bosch: BMA needs a delay of 450us after each write if it
	 * is in low power mode, otherwise the operation may be ignored by
	 * the sensor. Given we are only doing write during init, add
	 * the delay unconditionally.
	 */
	udelay(450);

	return ret;
}

/*
 * Set specific bit set to certain value of a 8-bit reg.
 */
static inline int bma5_set_reg8(const struct motion_sensor_t *s, int reg,
				uint8_t bits, int mask)
{
	int val;

	RETURN_ERROR(bma5_read8(s, reg, &val));

	val = (int)(((uint8_t)val & ~(uint8_t)mask) | bits);

	return bma5_write8(s, reg, val);
}

static int write_accel_offset(const struct motion_sensor_t *s, intv3_t v)
{
	int i, val;
	rotate_inv(v, *s->rot_standard_ref, v);
	for (i = X; i <= Z; i++) {
		/* BMA530 use int9 to save offset */
		val = round_divide((int64_t)v[i] * BMA5_OFFSET_ACC_DIV_MG,
				   BMA5_OFFSET_ACC_MULTI_MG);
		if (val > 255)
			val = 255;
		if (val < -256)
			val = -256;
		uint8_t lsb = val & 0xFF;
		/* MSB for int9 is only sign */
		uint8_t msb = (val >> 8) & 0x01; // 9th bit
		/* Write LSB and MSB to the corresponding registers */
		RETURN_ERROR(bma5_write8(s, BMA5_OFFSET_0_ADDR + i * 2, lsb));
		RETURN_ERROR(
			bma5_write8(s, BMA5_OFFSET_0_ADDR + i * 2 + 1, msb));
	}
	return EC_SUCCESS;
}

static int set_foc_config(struct motion_sensor_t *s)
{
	/* Disabling offset compensation */
	RETURN_ERROR(bma5_write8(s, BMA5_OFFSET_0_ADDR, 0x00));
	RETURN_ERROR(bma5_write8(s, BMA5_OFFSET_0_ADDR + 1, 0x00));
	RETURN_ERROR(bma5_write8(s, BMA5_OFFSET_2_ADDR, 0x00));
	RETURN_ERROR(bma5_write8(s, BMA5_OFFSET_2_ADDR + 1, 0x00));
	RETURN_ERROR(bma5_write8(s, BMA5_OFFSET_4_ADDR, 0x00));
	RETURN_ERROR(bma5_write8(s, BMA5_OFFSET_4_ADDR + 1, 0x00));

	/* Disable FIFO */
	RETURN_ERROR(bma5_write8(s, BMA5_FIFO_CONFIG_0_ADDR, 0));

	/* Set accelerometer configurations to 50Hz,CIC, continuous mode */
	RETURN_ERROR(
		bma5_write8(s, BMA5_ACCEL_CONF1_ADDR, BMA5_FOC_ACC_CONF_VAL));

	return EC_SUCCESS;
}

static int wait_and_read_data(struct motion_sensor_t *s, intv3_t v)
{
	int i;

	/* Retry 5 times */
	uint8_t reg_data[6] = { 0 }, try_cnt = 5;

	/* Check if data is ready */
	while (try_cnt && (!(reg_data[0] & BMA5_STAT_DATA_RDY_ACCEL_MSK))) {
		/* 20ms delay for 50Hz ODR */
		crec_msleep(20);

		/* Read the status register */
		RETURN_ERROR(i2c_read_block(s->port, s->i2c_spi_addr_flags,
					    BMA5_STATUS_ADDR, reg_data, 1));

		try_cnt--;
	}

	/* Read the sensor data */
	RETURN_ERROR(i2c_read_block(s->port, s->i2c_spi_addr_flags,
				    BMA5_DATA_0_ADDR, reg_data, 6));

	for (i = X; i <= Z; i++) {
		v[i] = (((int8_t)reg_data[i * 2 + 1]) << 8) |
		       (reg_data[i * 2] & 0xf0);

		/* Since the resolution is only 12 bits*/
		v[i] >>= 4;
	}

	rotate(v, *s->rot_standard_ref, v);

	return EC_SUCCESS;
}

static int8_t perform_accel_foc(struct motion_sensor_t *s, int *target,
				int sens_range)
{
	intv3_t accel_data, offset;

	/* Structure to store accelerometer data temporarily */
	intv3_t delta_value = { 0, 0, 0 };

	/* Variable to define count */
	uint8_t i, loop, sample_count = 0;

	for (loop = 0; loop < BMA5_FOC_SAMPLE_LIMIT; loop++) {
		RETURN_ERROR(wait_and_read_data(s, accel_data));

		sample_count++;

		/* Store the data in a temporary structure */
		delta_value[0] += accel_data[0] - target[X];
		delta_value[1] += accel_data[1] - target[Y];
		delta_value[2] += accel_data[2] - target[Z];
	}

	/*
	 * The data is in LSB so -> [(LSB)*1000*range/2^11*-1]
	 * (unit of offset:mg)
	 */
	for (i = X; i <= Z; ++i) {
		offset[i] = ((((delta_value[i] * 1000 * sens_range) /
			       sample_count) /
			      2048) *
			     -1);
	}

	RETURN_ERROR(write_accel_offset(s, offset));

	return EC_SUCCESS;
}

static int perform_calib(struct motion_sensor_t *s, int enable)
{
	uint8_t config[2];
	int fifo_conf;
	intv3_t target = { 0, 0, 0 };
	int sens_range = s->current_range;

	if (!enable)
		return EC_SUCCESS;

	/* Read accelerometer configurations */
	RETURN_ERROR(i2c_read_block(s->port, s->i2c_spi_addr_flags,
				    BMA5_ACCEL_CONF1_ADDR, config, 2));

	RETURN_ERROR(bma5_read8(s, BMA5_FIFO_CONFIG_0_ADDR, &fifo_conf));

	/* Perform calibration */
	RETURN_ERROR(set_foc_config(s));

	/* We calibrate considering Z axis is laid flat on the surface */
	target[Z] = BMA5_ACC_DATA_PLUS_1G(sens_range);

	RETURN_ERROR(perform_accel_foc(s, target, sens_range));

	/* Set the saved sensor configuration */
	RETURN_ERROR(i2c_write_block(s->port, s->i2c_spi_addr_flags,
				     BMA5_ACCEL_CONF1_ADDR, config, 2));

	RETURN_ERROR(bma5_write8(s, BMA5_FIFO_CONFIG_0_ADDR, fifo_conf));

	return EC_SUCCESS;
}

static int set_range(struct motion_sensor_t *s, int range, int round)
{
	int ret, range_reg_val;

	range_reg_val = BMA5_RANGE_TO_REG(range);

	/*
	 * If rounding flag is set then set the range_val to nearest
	 * valid value.
	 */
	if ((BMA5_REG_TO_RANGE(range_reg_val) < range) && round)
		range_reg_val = BMA5_RANGE_TO_REG(range * 2);

	mutex_lock(s->mutex);

	/* Determine the new value of control reg and attempt to write it. */
	ret = bma5_set_reg8(s, BMA5_ACCEL_CONF2_ADDR,
			    range_reg_val << BMA5_ACCEL_RANGE_POS,
			    BMA5_ACCEL_RANGE_MSK);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS)
		s->current_range = BMA5_REG_TO_RANGE(range_reg_val);

	mutex_unlock(s->mutex);

	return ret;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	return BMA5_16_BIT_RESOLUTION;
}

/**
 * Return the data rate in millihz corresponding to an acc_odr register value.
 *
 * The input value is assumed to be in the range 1 to 15 inclusive, which is the
 * entire range of documented values for acc_odr.
 */
static int bma5_reg_to_odr(uint8_t reg)
{
	/*
	 * Maximum data rate is 6.4 kHz (6400000 mHz) at register value 0xc.
	 * Reducing the register value by 1 halves data rate down to a minimum
	 * of 1.5625 Hz at reg=0. Right-shifting the maximum value according
	 * to the difference of the register value and maximum rate yields the
	 * actual ODR, easily proven exhaustively:
	 *
	 * >>> [6400000 >> (0xc - reg) for reg in range(0, 12)]
	 * [1562, ... 6400000]
	 *
	 * The register is documented to be valid only for values 0..=12, and
	 * we can only cause undefined behavior if the register value is too
	 * large;
	 */
	ASSERT(reg <= 12);
	return 6400000 >> (0xC - reg);
}

/**
 * Return a ACCEL_CONFIG register value for the given data rate in millihz.
 *
 * This always returns a valid acc_odr setting in the range of 1
 * (DATA_RATE_0_78HZ) to 12 (DATA_RATE_1600HZ) inclusive. Rounds down if the
 * requested rate cannot be exactly programmed.
 */
static uint8_t bma5_odr_to_reg(uint32_t odr)
{
	/*
	 * Clamp ODR to supported sample rates, because any value outside that
	 * range yields illegal register values. The lower bound is rounded up
	 * to the nearest millihertz, since rounding down puts it out of range,
	 * and the upper bound is limited to the highest non-reserved sample
	 * rate (higher rates are documented but marked as reserved).
	 */
	odr = MAX(1562 /* 50/32 Hz */, (int)odr);
	odr = MIN((int)odr, 6400000);

	/*
	 * reg_to_odr (above) is fairly easy to understand, but this operation
	 * is not as obvious to derive. It is derived algebraically, starting
	 * with the reg_to_odr expression:
	 *
	 * 1. odr = 6400000 >> (12 - reg)
	 * 2. Convert bitshift to division by an exponent so it can be
	 *    manipulated more clearly: odr = 6400000 / (2**(12 - reg))
	 * 3. Negate the exponent and replace division with multiplication:
	 *    odr = 6400000 * 2**(reg - 12)
	 * 4. Simplify by factoring out 2**12 (4096): odr = 1562.5 * 2** reg
	 * 5. Solve for reg: reg = log2( odr / 1562.5)
	 *
	 * To avoid integer truncation issues, the multiplication is scaled by a
	 * factor of 512 to yield log2(1024 * odr / 3125) - log2(512). For
	 * high ODRs (greater than 800 Hz) the intermediate 4096 * odr exceeds
	 * the range of a 32-bit integer, but losing bits in the division isn't
	 * a problem for sufficiently large values so if the intermediate would
	 * overflow 32 bits we instead skip the scaling by 512.
	 */
	uint32_t fp_shift = odr >= 800000 ? 0 : __fls(512);
	uint32_t intermediate = (2 << fp_shift) * odr / 3125;

	return __fls(intermediate) - fp_shift;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int round)
{
	int ret = 0;
	struct accelgyro_saved_data_t *data = s->drv_data;

	mutex_lock(s->mutex);

	if (rate == 0) {
		/* Disable accel */
		GOTO_ON_ERROR(out, bma5_write8(s, BMA5_ACC_CONF0_ADDR,
					       BMA5_DISABLE));
		data->odr = 0;
	} else {
		uint8_t odr_reg_val = bma5_odr_to_reg(rate);

		if (data->odr == 0) {
			/* Accel was disabled; enable it */
			GOTO_ON_ERROR(out, bma5_write8(s, BMA5_ACC_CONF0_ADDR,
						       BMA5_ENABLE));
		}

		if ((bma5_reg_to_odr(odr_reg_val) < rate) && round)
			/*
			 * Go up to the next highest data rate, but don't exceed
			 * the highest supported rate (odr_400 and higher are
			 * documented but marked as reserved).
			 */
			odr_reg_val = MIN(BMA5_OUTPUT_DATA_RATE_400HZ,
					  odr_reg_val + 1);

		/* Determine the new value of control reg and write it. */
		GOTO_ON_ERROR(out,
			      bma5_set_reg8(s, BMA5_ACCEL_CONF1_ADDR,
					    odr_reg_val << BMA5_ACCEL_ODR_POS,
					    BMA5_ACCEL_ODR_MSK));

		/* If successfully written, then save the new data rate. */
		data->odr = bma5_reg_to_odr(odr_reg_val);
	}

out:
	mutex_unlock(s->mutex);

	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s)
{
	struct accelgyro_saved_data_t *data = s->drv_data;

	return data->odr;
}

static int set_offset(const struct motion_sensor_t *s, const int16_t *offset,
		      int16_t temp)
{
	int ret;
	intv3_t v = { offset[X], offset[Y], offset[Z] };

	mutex_lock(s->mutex);

	ret = write_accel_offset(s, v);

	mutex_unlock(s->mutex);

	return ret;
}

static int get_offset(const struct motion_sensor_t *s, int16_t *offset,
		      int16_t *temp)
{
	int i, val, ret;
	intv3_t v;

	mutex_lock(s->mutex);

	for (i = X; i <= Z; i++) {
		ret = bma5_read8(s, BMA5_OFFSET_0_ADDR + i, &val);
		if (ret) {
			mutex_unlock(s->mutex);
			return ret;
		}

		if (val > 0x7f)
			val -= 256;

		v[i] = round_divide((int64_t)val * BMA5_OFFSET_ACC_MULTI_MG,
				    BMA5_OFFSET_ACC_DIV_MG);
	}

	mutex_unlock(s->mutex);

	/* Offset is in milli-g */
	rotate(v, *s->rot_standard_ref, v);
	offset[X] = v[X];
	offset[Y] = v[Y];
	offset[Z] = v[Z];

	*temp = (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP;

	return EC_SUCCESS;
}

/*
 * Convert raw sensor register values in acc to 3d vector v, applying the
 * sensor's standard rotation to the result.
 */
static void swizzle_sample_data(const struct motion_sensor_t *s,
				const uint8_t acc[6], intv3_t v)
{
	/*
	 * Convert acceleration to a signed 16-bit number. Note, based on
	 * the order of the registers:
	 *
	 * acc[0] = X_AXIS_LSB
	 * acc[1] = X_AXIS_MSB
	 * acc[2] = Y_AXIS_LSB
	 * acc[3] = Y_AXIS_MSB
	 * acc[4] = Z_AXIS_LSB
	 * acc[5] = Z_AXIS_MSB
	 */
	for (int i = X; i <= Z; i++) {
		uint16_t acc_msb = (uint16_t)acc[i * 2 + 1] << 8;
		v[i] = acc_msb | acc[i * 2];
	}
	rotate(v, *s->rot_standard_ref, v);
}

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t acc[6];
	int ret;

	mutex_lock(s->mutex);

	/* Read 6 bytes starting at X_AXIS_LSB. */
	ret = i2c_read_block(s->port, s->i2c_spi_addr_flags, BMA5_DATA_0_ADDR,
			     acc, 6);

	mutex_unlock(s->mutex);

	if (ret)
		return ret;

	swizzle_sample_data(s, acc, v);

	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, reg_val;
	struct accelgyro_saved_data_t *data = s->drv_data;

	/* This driver requires a mutex. Assert if mutex is not supplied. */
	ASSERT(s->mutex);

	/* Read accelerometer's CHID ID */
	RETURN_ERROR(bma5_read8(s, BMA5_CHIP_ID_ADDR, &reg_val));

	if (s->chip != MOTIONSENSE_CHIP_BMA530 || reg_val != BMA5_CHIP_ID)
		return EC_ERROR_HW_INTERNAL;

	mutex_lock(s->mutex);

	/*
	 * Disable accelerometer by default, set ODR to match. This avoids
	 * generating FIFO interrupts until we actually care about the data.
	 */
	GOTO_ON_ERROR(out, bma5_write8(s, BMA5_ACC_CONF0_ADDR, BMA5_DISABLE));
	data->odr = 0;

	/* Configure interrupt-driven acquisition if desired */
	if (IS_ENABLED(BMA5XY_USE_INTERRUPTS)) {
		GOTO_ON_ERROR(out, bma5_write8(s, BMA5_FIFO_CTRL_ADDR,
					       BMA5_FIFO_RST));
		/*
		 * Enable all interrupts on INT1 pin, active-low push-pull
		 * output, latched until status register read.
		 */
		GOTO_ON_ERROR(out, bma5_write8(s, BMA5_INT1_CONF_ADDR, 0x01));
		GOTO_ON_ERROR(out, bma5_write8(s, BMA5_FIFO_WM0, 0x06));
		GOTO_ON_ERROR(out, bma5_write8(s, BMA5_FIFO_WM1, 0x00));
		GOTO_ON_ERROR(out,
			      bma5_write8(s, BMA5_INT_MAP_DATA_ADDR,
					  // BMA5_INT1_DRDY |
					  BMA5_INT1_FWM | BMA5_INT1_FFULL));

		GOTO_ON_ERROR(out, s->drv->enable_interrupt(s, true));
	}

out:
	mutex_unlock(s->mutex);

	if (ret)
		return ret;

	return sensor_init_done(s);
}

#ifdef BMA5XY_USE_INTERRUPTS
static uint32_t last_irq_timestamp;

__maybe_unused static inline int bma5_read16(const struct motion_sensor_t *s,
					     const int reg, int *data_ptr)
{
	return i2c_read16(s->port, s->i2c_spi_addr_flags, reg, data_ptr);
}

static int bma5xy_enable_interrupt(const struct motion_sensor_t *s, bool enable)
{
	int ret;

	mutex_lock(s->mutex);

	/* enable the FIFO */
	GOTO_ON_ERROR(out, bma5_write8(s, BMA5_FIFO_CONFIG_0_ADDR,
				       BMA5_FIFO_ACC_EN));

out:
	mutex_unlock(s->mutex);
	return ret;
}

/* Handle IRQ from sensor: schedule read from task context */
test_mockable void bma5xy_interrupt(enum gpio_signal signal)
{
	__atomic_store_n(&last_irq_timestamp, __hw_clock_source_read(),
			 __ATOMIC_RELAXED);
	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCEL_BMA5XY_INT_EVENT);
}

/* Process FIFO data read from accel and push data to host */
static void process_fifo_data(struct motion_sensor_t *s, uint8_t *data,
			      size_t data_bytes, uint32_t timestamp)
{
	for (int i = 0; i < data_bytes;) {
		int *v = s->raw_xyz;

		if (data[i] == 0xA1) {
			/* 0xA1 means dedicated time frame; out of data */
			i += 4;
			break;
		}

		else if (data[i] == 0xCE) {
			swizzle_sample_data(s, &data[i + 1], v);
			i += 7;
		}

		else if (data[i] == 0xCF) {
			swizzle_sample_data(s, &data[i + 1], v);
			i += 10;
		}

		else {
			i++;
			break;
		}

		if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
		    s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
			v = s->spoof_xyz;

		if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
			struct ec_response_motion_sensor_data response = {
				.sensor_num = s - motion_sensors,
				.data = { v[X], v[Y], v[Z] },
			};
			motion_sense_fifo_stage_data(&response, s, 3,
						     timestamp);
		} else {
			motion_sense_push_raw_xyz(s);
		}
	}
}

/* Handle interrupt in task context */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	uint32_t irq_timestamp =
		__atomic_load_n(&last_irq_timestamp, __ATOMIC_RELAXED);
	bool read_any_data = false;
	int interrupt_status_reg, fifo_depth;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCEL_BMA5XY_INT_EVENT)) ||
	    motion_sensor_in_forced_mode(s)) {
		return EC_ERROR_NOT_HANDLED;
	}

	/* Read interrupt status, also clears pending IRQs */
	RETURN_ERROR(bma5_read8(s, BMA5_INT1_STATUS_0, &interrupt_status_reg));
	if ((interrupt_status_reg &
	     (BMA5_FFULL_INT | BMA5_FWM_INT | BMA5_ACC_DRDY_INT)) == 0) {
		return EC_ERROR_NOT_HANDLED;
	}

	RETURN_ERROR(bma5_read16(s, BMA5_FIFO_LENGTH_0_ADDR, &fifo_depth));
	while (fifo_depth > 0) {
		/* large enough buffer for 4 samples */
		uint8_t fifo_data[28];
		int fifo_read = MIN(ARRAY_SIZE(fifo_data), fifo_depth + 1);
		int ret;

		mutex_lock(s->mutex);
		ret = i2c_read_block(s->port, s->i2c_spi_addr_flags,
				     BMA5_FIFO_DATA_ADDR, fifo_data, fifo_read);
		fifo_depth -= (fifo_read - 1);
		mutex_unlock(s->mutex);
		if (ret)
			return ret;

		process_fifo_data(s, fifo_data, fifo_read, irq_timestamp);
		read_any_data = true;
	}

	if (IS_ENABLED(CONFIG_ACCEL_FIFO) && read_any_data) {
		motion_sense_fifo_commit_data();
	}

	/* BMA5 need to rewrite int status to register */
	RETURN_ERROR(bma5_write8(s, BMA5_INT1_STATUS_0, interrupt_status_reg));

	return EC_SUCCESS;
}
#endif /* BMA5XY_USE_INTERRUPTS */

const struct accelgyro_drv bma5_accel_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
	.perform_calib = perform_calib,
#ifdef BMA5XY_USE_INTERRUPTS
	.enable_interrupt = bma5xy_enable_interrupt,
	.interrupt = bma5xy_interrupt,
	.irq_handler = irq_handler,
#endif
};
