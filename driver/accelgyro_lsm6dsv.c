/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * LSM6DSV / LSM6DSVQ Accel and Gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "driver/accelgyro_lsm6dsv.h"
#include "hwtimer.h"
#include "math_util.h"
#include "motion_sense_fifo.h"
#include "task.h"
#include "timer.h"

#ifdef CONFIG_ACCEL_LSM6DSV_INT_EVENT
#define ACCEL_LSM6DSV_INT_ENABLE
#endif

#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

STATIC_IF(ACCEL_LSM6DSV_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;

STATIC_IF(ACCEL_LSM6DSV_INT_ENABLE)
uint32_t samples_to_discard[LSM6DSV_FIFO_DEV_NUM];

/**
 * @return output base register for sensor
 */
static inline int get_xyz_reg(enum motionsensor_type type)
{
	return LSM6DSV_ACCEL_OUT_X_L_ADDR -
	       (LSM6DSV_ACCEL_OUT_X_L_ADDR - LSM6DSV_GYRO_OUT_X_L_ADDR) * type;
}

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

static bool double_tap_enabled(const struct motion_sensor_t *s)
{
#ifdef CONFIG_GESTURE_HOST_DETECTION
	struct stprivate_data *data = s->drv_data;

	return data->enabled_activities & BIT(MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
#else
	/* Without the host interface, double-tap stays on once configured */
	return true;
#endif
}

static int update_odr(const struct motion_sensor_t *s, int rate)
{
	uint8_t ctrl_reg = LSM6DSV_ODR_REG(s->type);
	uint8_t reg_val = 0;

	if (rate > 0)
		reg_val = LSM6DSV_ODR_TO_REG(rate);

	/*
	 * The tap engine needs the accel to run at 480 Hz or above, even when
	 * the EC/AP requested ODR is 0 (so the EC task does not poll XYZ data
	 * while waiting for tap interrupts).
	 */
	if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP) &&
	    s->type == MOTIONSENSE_TYPE_ACCEL && double_tap_enabled(s) &&
	    reg_val < LSM6DSV_ODR_480HZ_VAL)
		reg_val = LSM6DSV_ODR_480HZ_VAL;

	return st_write_data_with_mask(s, ctrl_reg, LSM6DSV_ODR_MASK, reg_val);
}

#ifdef ACCEL_LSM6DSV_INT_ENABLE
static int fifo_disable(const struct motion_sensor_t *s)
{
	return st_raw_write8(s->port, s->i2c_spi_addr_flags,
			     LSM6DSV_FIFO_CTRL4_ADDR,
			     LSM6DSV_FIFO_MODE_BYPASS_VAL);
}

static int fifo_enable(const struct motion_sensor_t *s)
{
	return st_raw_write8(s->port, s->i2c_spi_addr_flags,
			     LSM6DSV_FIFO_CTRL4_ADDR,
			     LSM6DSV_FIFO_MODE_CONTINUOUS_VAL);
}

static int lsm6dsv_enable_interrupt(const struct motion_sensor_t *s,
				    bool enable)
{
	uint8_t value = 0;

	if (enable) {
		value = LSM6DSV_INT_FIFO_TH | LSM6DSV_INT_FIFO_OVR |
			LSM6DSV_INT_FIFO_FULL;
		RETURN_ERROR(fifo_enable(s));
	} else {
		RETURN_ERROR(fifo_disable(s));
	}
	return st_raw_write8(s->port, s->i2c_spi_addr_flags, LSM6DSV_INT1_CTRL,
			     value);
}

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
		/* Update the hardware ODR if the activity state changed. */
		if (update_odr(s, data->base.odr) != EC_SUCCESS)
			ret = EC_RES_UNAVAILABLE;
		samples_to_discard[s->type] = LSM6DSV_DISCARD_SAMPLES;
	}

	k_mutex_unlock(s->mutex);

	return ret;
}
#endif /* CONFIG_GESTURE_HOST_DETECTION */

static int config_interrupt(const struct motion_sensor_t *s)
{
#if defined(CONFIG_GESTURE_SENSOR_DOUBLE_TAP) && \
	defined(CONFIG_GESTURE_HOST_DETECTION)
	struct stprivate_data *data = s->drv_data;
#endif

	/* Configure FIFO watermark level */
	RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
				   LSM6DSV_FIFO_CTRL1_ADDR, 1));
#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
	/*
	 * Configure built-in double-tap detection with a pulsed (non-latched)
	 * interrupt on INT1 so servicing it in forced mode requires no I2C
	 * read.
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
	return s->drv->enable_interrupt(s, true);
}

static void push_fifo_data(struct motion_sensor_t *main_s, uint8_t *fifo,
			   uint32_t saved_ts)
{
	struct motion_sensor_t *sensor;
	uint8_t tag = (*fifo >> 3);
	int id;
	int *axis;
	uint8_t *ptr = fifo + LSM6DSV_TAG_SIZE;

	if (tag != LSM6DSV_GYRO_TAG && tag != LSM6DSV_ACC_TAG)
		return;

	id = (tag == LSM6DSV_GYRO_TAG) ? MOTIONSENSE_TYPE_GYRO :
					 MOTIONSENSE_TYPE_ACCEL;

	/* Discard samples every ODR change */
	if (samples_to_discard[id] > 0) {
		samples_to_discard[id]--;
		return;
	}

	sensor = main_s + id;
	axis = sensor->raw_xyz;

	st_normalize(sensor, axis, ptr);
	if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
	    sensor->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
		axis = sensor->spoof_xyz;
	if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
		struct ec_response_motion_sensor_data vect;

		vect.data[X] = axis[X];
		vect.data[Y] = axis[Y];
		vect.data[Z] = axis[Z];
		vect.flags = 0;
		vect.sensor_num = sensor - motion_sensors;
		motion_sense_fifo_stage_data(&vect, sensor, 3, saved_ts);
	} else {
		motion_sense_push_raw_xyz(sensor);
	}
}

static int load_fifo(struct motion_sensor_t *main_s, const uint16_t fifo_len,
		     const uint32_t timestamp)
{
	uint8_t fifo[LSM6DSV_FIFO_SAMPLE_SIZE];
	int i;

	for (i = 0; i < fifo_len; i++) {
		RETURN_ERROR(st_raw_read_n_noinc(
			main_s->port, main_s->i2c_spi_addr_flags,
			LSM6DSV_FIFO_DATA_ADDR_TAG, fifo, sizeof(fifo)));
		push_fifo_data(main_s, fifo, timestamp);
	}
	return EC_SUCCESS;
}

static int accelgyro_config_fifo(const struct motion_sensor_t *s,
				 uint8_t bdr_reg_val)
{
	int err;
	uint8_t fifo_odr_mask = LSM6DSV_FIFO_ODR_MASK(s);

	/* Changing in FIFO ODR configuration requires stopping FIFO first */
	err = fifo_disable(s);
	if (err != EC_SUCCESS)
		return err;

	/*
	 * If ODR changed we have to discard first samples; the counter is
	 * reset on every ODR change and cleared when samples are dropped.
	 */
	samples_to_discard[s->type] = LSM6DSV_DISCARD_SAMPLES;

	err = st_write_data_with_mask(s, LSM6DSV_FIFO_CTRL3_ADDR, fifo_odr_mask,
				      bdr_reg_val);
	if (err != EC_SUCCESS)
		return err;

	return fifo_enable(s);
}

/**
 * lsm6dsv_interrupt - interrupt from int1 pin of sensor
 */
test_mockable void lsm6dsv_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();
	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCEL_LSM6DSV_INT_EVENT);
}

/**
 * irq_handler - bottom half of the interrupt stack
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	uint32_t interrupt_timestamp = last_interrupt_timestamp;
	struct lsm6dsv_fstatus fsts;
	bool has_read_fifo = false;
	int fifo_len = 0;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCEL_LSM6DSV_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP) &&
	    double_tap_enabled(s)) {
		int status = 0;

		if (st_raw_read8(s->port, s->i2c_spi_addr_flags,
				 LSM6DSV_TAP_SRC_ADDR, &status) == EC_SUCCESS &&
		    (status & LSM6DSV_DOUBLE_TAP_IA))
			*event |= TASK_EVENT_MOTION_ACTIVITY_INTERRUPT(
				MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
	}

	if (motion_sensor_in_forced_mode(s))
		return EC_ERROR_NOT_HANDLED;

	do {
		RETURN_ERROR(st_raw_read_n_noinc(
			s->port, s->i2c_spi_addr_flags, LSM6DSV_FIFO_STS1_ADDR,
			(uint8_t *)&fsts, sizeof(fsts)));
		if (fsts.len & (LSM6DSV_FIFO_DATA_OVR | LSM6DSV_FIFO_FULL))
			CPRINTS("%s FIFO Overrun: %04x", s->name, fsts.len);

		fifo_len = fsts.len & LSM6DSV_FIFO_DIFF_MASK;
		if (fifo_len) {
			RETURN_ERROR(
				load_fifo(s, fifo_len, interrupt_timestamp));
			has_read_fifo = true;
		}
	} while (fifo_len != 0);

	if (IS_ENABLED(CONFIG_ACCEL_FIFO) && has_read_fifo)
		motion_sense_fifo_commit_data();

	return EC_SUCCESS;
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
	int err;
	uint8_t ctrl_reg, mask, reg_val;
	int newrange = range;

	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		int normalized;

		ctrl_reg = LSM6DSV_CTRL8_ADDR;
		mask = LSM6DSV_ACCEL_FS_MASK;

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
	} else {
		ctrl_reg = LSM6DSV_CTRL6_ADDR;
		mask = LSM6DSV_GYRO_FS_MASK;
		reg_val = MIN(LSM6DSV_GYRO_FS_REG(range),
			      LSM6DSV_GYRO_FS_MAX_REG_VAL);
		if (rnd && (range > LSM6DSV_GYRO_NORMALIZE_FS(reg_val)) &&
		    (reg_val < LSM6DSV_GYRO_FS_MAX_REG_VAL))
			reg_val++;
		newrange = LSM6DSV_GYRO_NORMALIZE_FS(reg_val);
	}

	k_mutex_lock(s->mutex, K_FOREVER);
	err = st_write_data_with_mask(s, ctrl_reg, mask, reg_val);
	if (err == EC_SUCCESS) {
		s->current_range = newrange;
		if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP) &&
		    s->type == MOTIONSENSE_TYPE_ACCEL)
			err = set_tap_threshold(s, newrange);
	}
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
	ret = update_odr(s, normalized_rate);
	if (ret == EC_SUCCESS) {
		data->base.odr = normalized_rate;
#ifdef ACCEL_LSM6DSV_INT_ENABLE
		ret = accelgyro_config_fifo(s, reg_val);
#endif
	}
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

	if (s->type == MOTIONSENSE_TYPE_ACCEL)
		*ready = (LSM6DSV_STS_XLDA_UP == (tmp & LSM6DSV_STS_XLDA_MASK));
	else
		*ready = (LSM6DSV_STS_GDA_UP == (tmp & LSM6DSV_STS_GDA_MASK));

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
					 get_xyz_reg(s->type), raw,
					 OUT_XYZ_SIZE));
	st_normalize(s, v, raw);
	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tmp;
	struct stprivate_data *data = s->drv_data;

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

	/*
	 * This sensor can be powered through an EC reboot, so the state of the
	 * sensor is unknown here so reset it.
	 * LSM6DSV supports both Acc & Gyro features
	 * Board will see two virtual sensor devices: Acc & Gyro
	 * Requirement: Acc need be init before Gyro
	 */
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		k_mutex_lock(s->mutex, K_FOREVER);

		/*
		 * Issue a global reset (SW_POR), which also resets embedded functions
		 * that another bus owner may have left enabled.
		 */
		ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
				    LSM6DSV_FUNC_CFG_ACCESS_ADDR,
				    LSM6DSV_SW_POR);
		if (ret != EC_SUCCESS)
			goto err_unlock;
		crec_msleep(LSM6DSV_SW_POR_DELAY_MS);

		/* Output data not updated until have been read */
		ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
				    LSM6DSV_CTRL3_ADDR,
				    LSM6DSV_BDU | LSM6DSV_IF_INC);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		/* Require interrupt to be active low */
		ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
				    LSM6DSV_IF_CFG_ADDR, LSM6DSV_H_LACTIVE);
		if (ret != EC_SUCCESS)
			goto err_unlock;

#ifdef ACCEL_LSM6DSV_INT_ENABLE
		ret = fifo_disable(s);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		ret = config_interrupt(s);
		if (ret != EC_SUCCESS)
			goto err_unlock;
#endif
		k_mutex_unlock(s->mutex);
	}

	/* Set default resolution common to Acc and Gyro */
	data->resol = LSM6DSV_RESOLUTION;
	return sensor_init_done(s);

err_unlock:
	k_mutex_unlock(s->mutex);
	CPRINTS("%s: MS Init type:0x%X Error", s->name, s->type);
	return ret;
}

#ifdef CONFIG_BODY_DETECTION
static int get_rms_noise(const struct motion_sensor_t *s)
{
	/*
	 * RMS | Acceleration RMS noise in normal/low-power mode
	 * FS = ±4 g | 1.3 mg(RMS)
	 */
	return 1300;
}
#endif

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
	.enable_interrupt = lsm6dsv_enable_interrupt,
	.irq_handler = irq_handler,
#ifdef CONFIG_GESTURE_HOST_DETECTION
	.manage_activity = manage_activity,
	.list_activities = st_list_activities,
#endif
#endif /* ACCEL_LSM6DSV_INT_ENABLE */
#ifdef CONFIG_BODY_DETECTION
	.get_rms_noise = get_rms_noise,
#endif
};
