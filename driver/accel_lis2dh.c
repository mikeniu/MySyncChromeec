/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Accelerometer module driver for Chrome EC 3D digital accelerometers:
 * LIS2DH/LIS2DH12/LNG2DM
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/accel_lis2dh.h"
#include "driver/stm_mems_common.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "task.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)

STATIC_IF(ACCEL_LIS2DH_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;
STATIC_IF(ACCEL_LIS2DH_INT_ENABLE)
bool click_active;

static bool is_tap_sensor(const struct motion_sensor_t *s)
{
#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
	return s == &motion_sensors[CONFIG_GESTURE_TAP_SENSOR];
#else
	return false;
#endif
}

static bool double_tap_enabled(const struct motion_sensor_t *s)
{
	if (!is_tap_sensor(s))
		return false;
#ifdef CONFIG_GESTURE_HOST_DETECTION
	{
		struct stprivate_data *data = s->drv_data;

		return data->enabled_activities &
		       BIT(MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
	}
#else
	/* Without the host interface, double-tap stays on once configured */
	return true;
#endif
}

/*
 * The click threshold LSB is the accel full scale / 128, so it has to follow
 * the range to keep the threshold at CONFIG_GESTURE_TAP_THRES_MG.
 */
static int set_tap_threshold(const struct motion_sensor_t *s, int range)
{
	if (!is_tap_sensor(s))
		return EC_SUCCESS;

	return st_write_data_with_mask(s, LIS2DH_CLICK_THS_ADDR,
				       LIS2DH_CLICK_THS_MASK,
				       LIS2DH_CLICK_THS_REG(range));
}

/**
 * lis2dh_enable_fifo - Enable/Disable FIFO in LIS2DH
 * @s: Motion sensor pointer
 * @mode: fifo_modes
 */
static __maybe_unused int lis2dh_enable_fifo(const struct motion_sensor_t *s,
					     enum lis2dh_fmode mode)
{
	if (mode == LIS2DH_FIFO_BYPASS_MODE)
		click_active = false;
	return st_write_data_with_mask(s, LIS2DH_FIFO_CTRL_ADDR,
				       LIS2DH_FIFO_MODE_MASK, mode);
}

/**
 * lis2dh_config_interrupt- Configure interrupt for supported features.
 * @s: Motion sensor pointer
 *
 * Must works with interface mutex locked
 */
static __maybe_unused int
lis2dh_config_interrupt(const struct motion_sensor_t *s)
{
	/* Configure FIFO watermark level. */
	RETURN_ERROR(st_write_data_with_mask(s, LIS2DH_FIFO_CTRL_ADDR,
					     LIS2DH_FIFO_THRESHOLD_MASK, 1));

	/* Connect FIFO Trigger to INT1 */
	RETURN_ERROR(st_write_data_with_mask(s, LIS2DH_FIFO_CTRL_ADDR,
					     LIS2DH_FIFO_CTRL_TR_MASK, 0));

	/* Interrupt trigger level of power-on-reset is HIGH */
	RETURN_ERROR(st_write_data_with_mask(
		s, LIS2DH_H_ACTIVE_ADDR, LIS2DH_H_ACTIVE_MASK, LIS2DH_EN_BIT));

	/* Enable FIFO */
	RETURN_ERROR(st_write_data_with_mask(s, LIS2DH_CTRL5_ADDR,
					     LIS2DH_CTRL5_FIFO_EN_MASK, 0x1));

	if (is_tap_sensor(s)) {
		/*
		 * Configure built-in double-click detection on Z axis with a
		 * pulsed (non-latched, LIR_Click = 0) interrupt on INT1.
		 */
		RETURN_ERROR(st_write_data_with_mask(s, LIS2DH_CTRL2_ADDR,
						     LIS2DH_CTRL2_HPCLICK,
						     LIS2DH_EN_BIT));
		RETURN_ERROR(st_raw_write8(
			s->port, s->i2c_spi_addr_flags, LIS2DH_CLICK_THS_ADDR,
			LIS2DH_CLICK_THS_REG(BASE_RANGE(s->default_range))));
		RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
					   LIS2DH_TIME_LIMIT_ADDR,
					   LIS2DH_TIME_LIMIT_VAL));
		RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
					   LIS2DH_TIME_LATENCY_ADDR,
					   LIS2DH_TIME_LATENCY_VAL));
		RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
					   LIS2DH_TIME_WINDOW_ADDR,
					   LIS2DH_TIME_WINDOW_VAL));
		RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
					   LIS2DH_CLICK_CFG_ADDR,
					   LIS2DH_CLICK_ZD));
		RETURN_ERROR(st_write_data_with_mask(s, LIS2DH_INT1_CLICK_ADDR,
						     LIS2DH_INT1_CLICK_MASK,
						     LIS2DH_EN_BIT));
#ifdef CONFIG_GESTURE_HOST_DETECTION
		{
			struct stprivate_data *data = s->drv_data;

			data->enabled_activities |=
				BIT(MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
			data->disabled_activities &=
				~BIT(MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
		}
#endif
	}

	RETURN_ERROR(
		s->drv->enable_interrupt(s, !motion_sensor_in_forced_mode(s)));

	return EC_SUCCESS;
}

#ifdef ACCEL_LIS2DH_INT_ENABLE
/**
 * Load data from internal sensor FIFO.
 * @s: Motion sensor pointer
 */
static int lis2dh_load_fifo(struct motion_sensor_t *s, int nsamples,
			    uint32_t timestamp)
{
	int ret, left, length, i;
	int *axis = s->raw_xyz;
	uint8_t fifo[FIFO_READ_LEN];
	int read_count;
	uint32_t ts;

	/* Each sample are OUT_XYZ_SIZE bytes. */
	left = nsamples * OUT_XYZ_SIZE;

	do {
		/*
		 * Limit FIFO read data to burst of FIFO_READ_LEN size because
		 * read operations in under i2c mutex lock.
		 */
		if (left > FIFO_READ_LEN)
			length = FIFO_READ_LEN;
		else
			length = left;

		ret = st_raw_read_n(s->port, s->i2c_spi_addr_flags,
				    LIS2DH_OUT_X_L_ADDR, fifo, length);
		if (ret != EC_SUCCESS)
			return ret;

		read_count = 0;
		for (i = 0; i < length; i += OUT_XYZ_SIZE) {
			/* Apply precision, sensitivity and rotation vector. */
			st_normalize(s, axis, &fifo[i]);

			if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
			    s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE) {
				axis = s->spoof_xyz;
			}
			if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
				struct ec_response_motion_sensor_data vect;
				/* Fill vector array. */
				vect.data[X] = axis[X];
				vect.data[Y] = axis[Y];
				vect.data[Z] = axis[Z];
				vect.flags = 0;
				vect.sensor_num = s - motion_sensors;

				/*
				 * TODO (b/522434347): The minimum number of
				 * samples that will be read from the fifo is 2
				 * because interrupts are triggering when fss >
				 * fth. Because the motionsense fifo needs to
				 * pair each sample with a unique timestamp,
				 * generate a timestamp for the 1st sample from
				 * the timestamp that was latched in the
				 * interrupt handler for the 2nd sample.
				 */
				read_count++;
				ts = nsamples > 1 ?
					     timestamp - s->collection_rate *
								 (nsamples -
								  read_count) :
					     timestamp;
				motion_sense_fifo_stage_data(&vect, s, 3, ts);
			} else {
				motion_sense_push_raw_xyz(s);
			}
			left -= length;
		}
	} while (left > 0);

	return EC_SUCCESS;
}

/**
 * lis2dh_get_fifo_samples - check for stored FIFO samples.
 */
static int lis2dh_get_fifo_samples(struct motion_sensor_t *s, int *nsamples)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LIS2DH_FIFO_SRC_ADDR,
			   &tmp);
	if (ret != EC_SUCCESS) {
		return ret;
	}

	*nsamples = tmp & LIS2DH_FIFO_DIFF_MASK;

	return EC_SUCCESS;
}

/**
 * lis2dh_interrupt - interrupt from int pin of sensor
 * Schedule Motion Sense Task to manage Interrupts.
 */
test_mockable void lis2dh_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE, ACCEL_LIS2DH_INT_EVENT);
}

/**
 * lis2dh_irq_handler - bottom half of the interrupt stack.
 */
static int lis2dh_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	uint32_t interrupt_timestamp = last_interrupt_timestamp;
	bool commit_needed = false;
	int nsamples;

	if (!(*event & ACCEL_LIS2DH_INT_EVENT))
		return EC_ERROR_NOT_HANDLED;

	if (double_tap_enabled(s)) {
		struct stprivate_data *data = s->drv_data;
		int status = 0;

		if (st_raw_read8(s->port, s->i2c_spi_addr_flags,
				 LIS2DH_CLICK_SRC_ADDR,
				 &status) == EC_SUCCESS) {
			bool dclick = status & LIS2DH_CLICK_SRC_DCLICK;

			if (dclick && !click_active)
				*event |= TASK_EVENT_MOTION_ACTIVITY_INTERRUPT(
					MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
			click_active = dclick &&
				       !motion_sensor_in_forced_mode(s) &&
				       (data->base.odr > 0);
		}
	}

	if (motion_sensor_in_forced_mode(s))
		return EC_ERROR_NOT_HANDLED;

	do {
		RETURN_ERROR(lis2dh_get_fifo_samples(s, &nsamples));

		if (nsamples != 0) {
			commit_needed = true;
			RETURN_ERROR(lis2dh_load_fifo(s, nsamples,
						      interrupt_timestamp));
		}
	} while (nsamples != 0);

	if (IS_ENABLED(CONFIG_ACCEL_FIFO) && commit_needed) {
		motion_sense_fifo_commit_data();
	}

	return EC_SUCCESS;
}

static int lis2dh_enable_interrupt(const struct motion_sensor_t *s, bool enable)
{
	struct stprivate_data *data = s->drv_data;

	if (!enable)
		RETURN_ERROR(lis2dh_enable_fifo(s, LIS2DH_FIFO_BYPASS_MODE));

	/*
	 * Route I1_CLICK to INT1 only when the FIFO watermark interrupt is
	 * inactive (!enable or ODR == 0); when FIFO streaming is active,
	 * I1_WTM already wakes lis2dh_irq_handler() every 5 ms to poll
	 * CLICK_SRC without holding INT1 low across watermark intervals.
	 */
	if (is_tap_sensor(s))
		RETURN_ERROR(st_write_data_with_mask(
			s, LIS2DH_INT1_CLICK_ADDR, LIS2DH_INT1_CLICK_MASK,
			(!enable || data->base.odr == 0) &&
				double_tap_enabled(s)));

	/* Enable interrupt on FIFO watermark and route to int1. */
	RETURN_ERROR(st_write_data_with_mask(s, LIS2DH_INT1_FTH_ADDR,
					     LIS2DH_INT1_FTH_MASK, enable));

	return EC_SUCCESS;
}
#endif

/**
 * set_range - set full scale range
 * @s: Motion sensor pointer
 * @range: Range
 * @rnd: Round up/down flag
 */
static int set_range(struct motion_sensor_t *s, int range, int rnd)
{
	struct stprivate_data *data = s->drv_data;
	int normalized_range;
	int val;
	int err = EC_SUCCESS;

	val = LIS2DH_FS_TO_REG(range);
	normalized_range = ST_NORMALIZE_RATE(range);

	if (rnd && (range < normalized_range))
		val++;

	/* Adjust rounded values */
	if (val > LIS2DH_FS_16G_VAL) {
		val = LIS2DH_FS_16G_VAL;
		normalized_range = 16;
	}

	if (val < LIS2DH_FS_2G_VAL) {
		val = LIS2DH_FS_2G_VAL;
		normalized_range = 2;
	}

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	k_mutex_lock(s->mutex, K_FOREVER);
	/*
	 * FIFO stop collecting events. Restart FIFO in Bypass mode.
	 * If Range is changed all samples in FIFO must be discharged because
	 * with a different sensitivity.
	 */
	if (IS_ENABLED(ACCEL_LIS2DH_INT_ENABLE)) {
		err = lis2dh_enable_fifo(s, LIS2DH_FIFO_BYPASS_MODE);
		if (err != EC_SUCCESS)
			goto unlock_rate;
	}

	err = st_write_data_with_mask(s, LIS2DH_CTRL4_ADDR, LIS2DH_FS_MASK,
				      val);

	/* Save Gain in range for speed up data path */
	if (err == EC_SUCCESS) {
		s->current_range = normalized_range;
		err = set_tap_threshold(s, normalized_range);
		if (err != EC_SUCCESS)
			goto unlock_rate;
	} else {
		goto unlock_rate;
	}

	/* FIFO restart collecting events in Stream  mode. */
	if (IS_ENABLED(ACCEL_LIS2DH_INT_ENABLE) &&
	    !motion_sensor_in_forced_mode(s) &&
	    (data->base.odr > 0 || !double_tap_enabled(s)))
		err = lis2dh_enable_fifo(s, LIS2DH_STREAM_MODE);

unlock_rate:
	k_mutex_unlock(s->mutex);

	return err;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate;
	struct stprivate_data *data = s->drv_data;
	uint8_t reg_val;

	k_mutex_lock(s->mutex, K_FOREVER);

	/*
	 * FIFO stop collecting events. Set FIFO to bypass mode to flush the
	 * lis2dh FIFO.
	 *
	 * If ODR is changed all samples in FIFO must be discharged because
	 * they were captured at the previous rate and the motion sense FIFO
	 * spreading algorithm would otherwise stage them with timestamps
	 * spaced at the new period. This mirrors what set_range() already
	 * does for full-scale changes, and what accel_lis2dw12.c does in
	 * its set_data_rate().
	 */
	if (IS_ENABLED(ACCEL_LIS2DH_INT_ENABLE)) {
		ret = lis2dh_enable_fifo(s, LIS2DH_FIFO_BYPASS_MODE);
		if (ret != EC_SUCCESS) {
			goto unlock_rate;
		}
	}

	if (rate == 0) {
		/*
		 * Keep the accelerometer running at 400 Hz when double-tap
		 * detection is active while leaving the FIFO in bypass mode.
		 */
		reg_val = double_tap_enabled(s) ? LIS2DH_ODR_400HZ_VAL :
						  LIS2DH_ODR_0HZ_VAL;
		ret = st_write_data_with_mask(s, LIS2DH_CTRL1_ADDR,
					      LIS2DH_ACC_ODR_MASK, reg_val);
		if (ret == EC_SUCCESS) {
			data->base.odr = 0;
			if (IS_ENABLED(ACCEL_LIS2DH_INT_ENABLE) &&
			    is_tap_sensor(s))
				ret = st_write_data_with_mask(
					s, LIS2DH_INT1_CLICK_ADDR,
					LIS2DH_INT1_CLICK_MASK,
					double_tap_enabled(s));
		}
		goto unlock_rate;
	}

	reg_val = LIS2DH_ODR_TO_REG(rate);
	normalized_rate = LIS2DH_ODR_TO_NORMALIZE(rate);

	if (rnd && (normalized_rate < rate)) {
		reg_val++;
		normalized_rate = LIS2DH_REG_TO_NORMALIZE(reg_val);
	}

	if (normalized_rate > LIS2DH_ODR_MAX_VAL ||
	    normalized_rate < LIS2DH_ODR_MIN_VAL) {
		ret = EC_RES_INVALID_PARAM;
		goto unlock_rate;
	}

	if (double_tap_enabled(s) && reg_val < LIS2DH_ODR_400HZ_VAL) {
		reg_val = LIS2DH_ODR_400HZ_VAL;
		if (!motion_sensor_in_forced_mode(s))
			normalized_rate = LIS2DH_REG_TO_NORMALIZE(reg_val);
	}

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done
	 */
	ret = st_write_data_with_mask(s, LIS2DH_CTRL1_ADDR, LIS2DH_ACC_ODR_MASK,
				      reg_val);
	if (ret == EC_SUCCESS)
		data->base.odr = normalized_rate;

	/* FIFO restart collecting events in Stream mode. */
	if (IS_ENABLED(ACCEL_LIS2DH_INT_ENABLE)) {
		if (is_tap_sensor(s)) {
			ret = st_write_data_with_mask(
				s, LIS2DH_INT1_CLICK_ADDR,
				LIS2DH_INT1_CLICK_MASK,
				motion_sensor_in_forced_mode(s) &&
					double_tap_enabled(s));
			if (ret != EC_SUCCESS)
				goto unlock_rate;
		}
		if (!motion_sensor_in_forced_mode(s))
			ret = lis2dh_enable_fifo(s, LIS2DH_STREAM_MODE);
	}

unlock_rate:
	k_mutex_unlock(s->mutex);
	return ret;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LIS2DH_STATUS_REG,
			   &tmp);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RS Error", s->name, s->type);
		return ret;
	}

	*ready = (LIS2DH_STS_XLDA_UP == (tmp & LIS2DH_STS_XLDA_UP));

	return EC_SUCCESS;
}

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t raw[OUT_XYZ_SIZE];
	int ret, tmp = 0;

	ret = is_data_ready(s, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * If sensor data is not ready, return the previous read data.
	 * Note: return success so that motion senor task can read again
	 * to get the latest updated sensor data quickly.
	 */
	if (!tmp) {
		if (v != s->raw_xyz)
			memcpy(v, s->raw_xyz, sizeof(s->raw_xyz));
		return EC_SUCCESS;
	}

	/* Read output data bytes starting at LIS2DH_OUT_X_L_ADDR */
	ret = st_raw_read_n(s->port, s->i2c_spi_addr_flags, LIS2DH_OUT_X_L_ADDR,
			    raw, OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RD XYZ Error", s->name, s->type);
		return ret;
	}

	/* Transform from LSB to real data with rotation and gain */
	st_normalize(s, v, raw);

	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tmp;
	struct stprivate_data *data = s->drv_data;
	int count = 10;

	/*
	 * lis2de need 5 milliseconds to complete boot procedure after
	 * device power-up. When sensor is powered on, it can't be
	 * accessed immediately. We need wait serval loops to let sensor
	 * complete boot procedure.
	 */
	do {
		ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
				   LIS2DH_WHO_AM_I_REG, &tmp);
		if (ret != EC_SUCCESS) {
			udelay(10);
			count--;
		} else {
			break;
		}
	} while (count > 0);

	if (ret != EC_SUCCESS)
		return ret;

	if (tmp != LIS2DH_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	k_mutex_lock(s->mutex, K_FOREVER);
	/*
	 * Device can be re-initialized after a reboot so any control
	 * register must be restored to it's default.
	 */
	/* Enable all accel axes data and clear old settings */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags, LIS2DH_CTRL1_ADDR,
			    LIS2DH_ENABLE_ALL_AXES);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags, LIS2DH_CTRL2_ADDR,
			    LIS2DH_CTRL2_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags, LIS2DH_CTRL3_ADDR,
			    LIS2DH_CTRL3_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	/*
	 * Enable BDU and attempt to enable High-Resolution (HR) mode.
	 * On LIS2DH, bit 3 (HR) is writable and enables 12-bit resolution.
	 * On LIS2DE/LNG2DM, bit 3 is read-only 0 (8-bit only).
	 */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags, LIS2DH_CTRL4_ADDR,
			    LIS2DH_BDU_MASK | LIS2DH_HR_MASK);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LIS2DH_CTRL4_ADDR,
			   &tmp);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	if (tmp & LIS2DH_HR_MASK) {
		data->resol = LIS2DH_RESOLUTION_12;
	} else {
		data->resol = LIS2DH_RESOLUTION_8;
	}

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags, LIS2DH_CTRL5_ADDR,
			    LIS2DH_CTRL5_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags, LIS2DH_CTRL6_ADDR,
			    LIS2DH_CTRL6_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	if (IS_ENABLED(ACCEL_LIS2DH_INT_ENABLE)) {
		ret = lis2dh_config_interrupt(s);
		if (ret != EC_SUCCESS) {
			goto err_unlock;
		}
	}

	k_mutex_unlock(s->mutex);

	return sensor_init_done(s);

err_unlock:
	k_mutex_unlock(s->mutex);
	CPRINTS("%s: MS Init type:0x%X Error", s->name, s->type);

	return ret;
}

#if defined(ACCEL_LIS2DH_INT_ENABLE) && defined(CONFIG_GESTURE_HOST_DETECTION)
/* The return value goes back to the host, so it is an EC_RES_* code. */
static int manage_activity(const struct motion_sensor_t *s,
			   enum motionsensor_activity activity, int enable,
			   const struct ec_motion_sense_activity *param)
{
	int ret = EC_RES_INVALID_PARAM;
	struct stprivate_data *data = s->drv_data;

	if (!is_tap_sensor(s))
		return EC_RES_INVALID_PARAM;

	switch (activity) {
#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
	case MOTIONSENSE_ACTIVITY_DOUBLE_TAP:
		k_mutex_lock(s->mutex, K_FOREVER);
		ret = st_write_data_with_mask(
			s, LIS2DH_CLICK_CFG_ADDR, LIS2DH_CLICK_ZD,
			enable ? LIS2DH_EN_BIT : LIS2DH_DIS_BIT);
		k_mutex_unlock(s->mutex);
		if (ret != EC_SUCCESS)
			return EC_RES_UNAVAILABLE;
		ret = EC_RES_SUCCESS;
		break;
#endif
	default:
		return EC_RES_INVALID_PARAM;
	}

	if (enable) {
		data->enabled_activities |= BIT(activity);
		data->disabled_activities &= ~BIT(activity);
	} else {
		data->enabled_activities &= ~BIT(activity);
		data->disabled_activities |= BIT(activity);
	}

	if (set_data_rate(s, data->base.odr, 0) != EC_SUCCESS)
		return EC_RES_UNAVAILABLE;

	return ret;
}
#endif

const struct accelgyro_drv lis2dh_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = st_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = st_get_data_rate,
	.set_offset = st_set_offset,
	.get_offset = st_get_offset,
#ifdef ACCEL_LIS2DH_INT_ENABLE
	.enable_interrupt = lis2dh_enable_interrupt,
	.irq_handler = lis2dh_irq_handler,
#ifdef CONFIG_GESTURE_HOST_DETECTION
	.manage_activity = manage_activity,
	.list_activities = st_list_activities,
#endif
#endif /* ACCEL_LIS2DH_INT_ENABLE */
};
