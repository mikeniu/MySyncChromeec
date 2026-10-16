/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Minimal LSM6DSV / LSM6DSVQ Accel module for Chrome EC
 */

#include "driver/accelgyro_lsm6dsv.h"
#include "math_util.h"
#include "task.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

/**
 * set_range - set full scale range
 * @s: Motion sensor pointer
 * @range: Range
 * @rnd: Round up/down flag
 */
static int set_range(struct motion_sensor_t *s, int range, int rnd)
{
	int err, normalized;
	uint8_t reg_val;
	int newrange = range;

	/*
	 * Clamp first because the FS macros use __builtin_clz(), which is
	 * undefined for 0.
	 */
	newrange = CLAMP(newrange, LSM6DSV_ACCEL_FS_MIN_VAL,
			 LSM6DSV_ACCEL_FS_MAX_VAL);

	normalized = LSM6DSV_ACCEL_NORMALIZE_FS(newrange);
	if (rnd && (normalized < newrange))
		normalized <<= 1;

	newrange = normalized;
	reg_val = LSM6DSV_ACCEL_FS_REG(newrange);

	k_mutex_lock(s->mutex, K_FOREVER);
	err = st_write_data_with_mask(s, LSM6DSV_CTRL8_ADDR,
				      LSM6DSV_ACCEL_FS_MASK, reg_val);
	if (err == EC_SUCCESS)
		s->current_range = newrange;
	k_mutex_unlock(s->mutex);

	return err;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate = 0;
	struct stprivate_data *data = s->drv_data;
	uint8_t reg_val = 0;

	if (rate > 0) {
		if (rate < LSM6DSV_ODR_MIN_VAL) {
			if (!rnd)
				return EC_ERROR_INVAL;
			rate = LSM6DSV_ODR_MIN_VAL;
		}
		reg_val = LSM6DSV_ODR_TO_REG(rate);
		normalized_rate = LSM6DSV_REG_TO_ODR(reg_val);

		if (rnd && (normalized_rate < rate)) {
			reg_val++;
			normalized_rate = LSM6DSV_REG_TO_ODR(reg_val);
		}

		if (normalized_rate < LSM6DSV_ODR_MIN_VAL ||
		    normalized_rate > LSM6DSV_ODR_MAX_VAL)
			return EC_ERROR_INVAL;
	}

	k_mutex_lock(s->mutex, K_FOREVER);
	ret = st_write_data_with_mask(s, LSM6DSV_CTRL1_ADDR, LSM6DSV_ODR_MASK,
				      reg_val);
	if (ret == EC_SUCCESS)
		data->base.odr = normalized_rate;
	k_mutex_unlock(s->mutex);

	return ret;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LSM6DSV_STATUS_REG,
			   &tmp);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RS Error", s->name, s->type);
		return ret;
	}

	*ready = (LSM6DSV_STS_XLDA_UP == (tmp & LSM6DSV_STS_XLDA_MASK));
	return EC_SUCCESS;
}

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t raw[OUT_XYZ_SIZE];
	int tmp = 0;

	RETURN_ERROR(is_data_ready(s, &tmp));
	if (!tmp) {
		if (v != s->raw_xyz)
			memcpy(v, s->raw_xyz, sizeof(s->raw_xyz));
		return EC_SUCCESS;
	}

	RETURN_ERROR(st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
					 LSM6DSV_ACCEL_OUT_X_L_ADDR, raw,
					 OUT_XYZ_SIZE));
	st_normalize(s, v, raw);
	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tmp;
	struct stprivate_data *data = s->drv_data;

	if (s->type != MOTIONSENSE_TYPE_ACCEL)
		return EC_ERROR_INVAL;

	/*
	 * Select the user register bank first, as the sensor may be shared with
	 * the AP and left on another bank where WHO_AM_I and the reset bits are
	 * not accessible.
	 */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LSM6DSV_FUNC_CFG_ACCESS_ADDR, LSM6DSV_USER_BANK);
	if (ret != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LSM6DSV_WHO_AM_I_REG,
			   &tmp);
	if (ret != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	if (tmp != LSM6DSV_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	k_mutex_lock(s->mutex, K_FOREVER);

	/*
	 * Issue a global reset (SW_POR), which also resets embedded functions
	 * that another bus owner may have left enabled.
	 */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LSM6DSV_FUNC_CFG_ACCESS_ADDR, LSM6DSV_SW_POR);
	if (ret != EC_SUCCESS)
		goto err_unlock;
	crec_msleep(LSM6DSV_SW_POR_DELAY_MS);

	/* Output data not updated until have been read */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags, LSM6DSV_CTRL3_ADDR,
			    LSM6DSV_BDU | LSM6DSV_IF_INC);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	/* Require interrupt to be active low */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags, LSM6DSV_IF_CFG_ADDR,
			    LSM6DSV_H_LACTIVE);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	k_mutex_unlock(s->mutex);

	data->resol = LSM6DSV_RESOLUTION;
	return sensor_init_done(s);

err_unlock:
	k_mutex_unlock(s->mutex);
	CPRINTS("%s: MS Init type:0x%X Error", s->name, s->type);
	return ret;
}

const struct accelgyro_drv lsm6dsv_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = st_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = st_get_data_rate,
	.set_offset = st_set_offset,
	.get_offset = st_get_offset,
};
