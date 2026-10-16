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

#ifdef CONFIG_ACCEL_LSM6DSV_INT_EVENT
#define ACCEL_LSM6DSV_INT_ENABLE
#endif

#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

/*
 * The tap threshold LSB is the accel full scale / 32, so it has to follow
 * the range to keep the threshold at CONFIG_GESTURE_TAP_THRES_MG.
 */
static int set_tap_threshold(const struct motion_sensor_t *s, int range)
{
#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
	uint8_t ths = LSM6DSV_TAP_THS_REG(range);

	RETURN_ERROR(st_write_data_with_mask(s, LSM6DSV_TAP_CFG1_ADDR,
					     LSM6DSV_TAP_THS_MASK, ths));
	RETURN_ERROR(st_write_data_with_mask(s, LSM6DSV_TAP_CFG2_ADDR,
					     LSM6DSV_TAP_THS_MASK, ths));
	return st_write_data_with_mask(s, LSM6DSV_TAP_THS_6D_ADDR,
				       LSM6DSV_TAP_THS_MASK, ths);
#else
	return EC_SUCCESS;
#endif
}

#ifdef ACCEL_LSM6DSV_INT_ENABLE
#ifdef CONFIG_GESTURE_HOST_DETECTION
/* The return value goes back to the host, so it is an EC_RES_* code. */
static int manage_activity(const struct motion_sensor_t *s,
			   enum motionsensor_activity activity, int enable,
			   const struct ec_motion_sense_activity *param)
{
	int ret = EC_RES_INVALID_PARAM;
	struct stprivate_data *data = s->drv_data;

	k_mutex_lock(s->mutex, K_FOREVER);

	switch (activity) {
#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
	case MOTIONSENSE_ACTIVITY_DOUBLE_TAP:
		if (st_write_data_with_mask(
			    s, LSM6DSV_MD1_CFG_ADDR, LSM6DSV_INT1_DOUBLE_TAP,
			    enable ? LSM6DSV_EN_BIT : LSM6DSV_DIS_BIT))
			ret = EC_RES_UNAVAILABLE;
		else
			ret = EC_RES_SUCCESS;
		break;
#endif
	default:
		break;
	}

	if (ret == EC_RES_SUCCESS) {
		if (enable) {
			data->enabled_activities |= BIT(activity);
			data->disabled_activities &= ~BIT(activity);
		} else {
			data->enabled_activities &= ~BIT(activity);
			data->disabled_activities |= BIT(activity);
		}
	}

	k_mutex_unlock(s->mutex);

	return ret;
}
#endif /* CONFIG_GESTURE_HOST_DETECTION */

static int config_interrupt(const struct motion_sensor_t *s)
{
#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
#ifdef CONFIG_GESTURE_HOST_DETECTION
	struct stprivate_data *data = s->drv_data;
#endif
	/*
	 * Configure built-in double-tap detection with a pulsed (non-latched)
	 * interrupt on INT1 so servicing it requires no I2C read.
	 */
	RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
				   LSM6DSV_TAP_CFG0_ADDR, LSM6DSV_TAP_XYZ_EN));
	RETURN_ERROR(set_tap_threshold(s, BASE_RANGE(s->current_range)));
	RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
				   LSM6DSV_TAP_DUR_ADDR, LSM6DSV_TAP_DUR_VAL));
	RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
				   LSM6DSV_WAKE_UP_THS_ADDR,
				   LSM6DSV_SINGLE_DOUBLE_TAP));
	RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
				   LSM6DSV_FUNCTIONS_ENABLE_ADDR,
				   LSM6DSV_INTERRUPTS_ENABLE));
	RETURN_ERROR(st_write_data_with_mask(s, LSM6DSV_MD1_CFG_ADDR,
					     LSM6DSV_INT1_DOUBLE_TAP,
					     LSM6DSV_EN_BIT));
#ifdef CONFIG_GESTURE_HOST_DETECTION
	data->enabled_activities |= BIT(MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
	data->disabled_activities &= ~BIT(MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
#endif
#endif /* CONFIG_GESTURE_SENSOR_DOUBLE_TAP */
	return EC_SUCCESS;
}

/**
 * lsm6dsv_interrupt - interrupt from int1 pin of sensor
 */
test_mockable void lsm6dsv_interrupt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCEL_LSM6DSV_INT_EVENT);
}

/**
 * irq_handler - bottom half of the interrupt stack
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCEL_LSM6DSV_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	/*
	 * Double-tap is the only source routed to INT1 and the pulsed
	 * interrupt (LIR = 0) self-clears without an I2C read.
	 */
	if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP))
		*event |= TASK_EVENT_MOTION_ACTIVITY_INTERRUPT(
			MOTIONSENSE_ACTIVITY_DOUBLE_TAP);

	return EC_ERROR_NOT_HANDLED;
}
#endif /* ACCEL_LSM6DSV_INT_ENABLE */

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
	if (err == EC_SUCCESS) {
		s->current_range = newrange;
		if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP))
			err = set_tap_threshold(s, newrange);
	}
	k_mutex_unlock(s->mutex);

	return err;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate = 0;
	struct stprivate_data *data = s->drv_data;
	uint8_t reg_val = 0, hw_odr_val = 0;

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

	hw_odr_val = reg_val;
	if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP) &&
	    hw_odr_val < LSM6DSV_ODR_480HZ_VAL)
		hw_odr_val = LSM6DSV_ODR_480HZ_VAL;

	k_mutex_lock(s->mutex, K_FOREVER);
	ret = st_write_data_with_mask(s, LSM6DSV_CTRL1_ADDR, LSM6DSV_ODR_MASK,
				      hw_odr_val);
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

#ifdef ACCEL_LSM6DSV_INT_ENABLE
	ret = config_interrupt(s);
	if (ret != EC_SUCCESS)
		goto err_unlock;
#endif
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
#ifdef ACCEL_LSM6DSV_INT_ENABLE
	.irq_handler = irq_handler,
#ifdef CONFIG_GESTURE_HOST_DETECTION
	.manage_activity = manage_activity,
	.list_activities = st_list_activities,
#endif
#endif /* ACCEL_LSM6DSV_INT_ENABLE */
};
