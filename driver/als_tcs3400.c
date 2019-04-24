/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMS TCS3400 light sensor driver
 */
#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/als_tcs3400.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"
#include "util.h"

#define CPRINTS(fmt, args...) cprints(CC_ACCEL, "%s "fmt, __func__, ## args)

enum alslog_level {
	DISABLED = 0,
	ERRORS = 1,
	PRIORITY = 2,
	RAW_DATA = 4,
	SATURATION = 8,
	DEBUG = 16,
	VERBOSE = 32,
};

#ifdef CONFIG_CMD_ALSLOG
static int gAlsLogMask;
static int command_log_als_data(int argc, char **argv)
{
	/* toggle log state */
	gAlsLogMask = (gAlsLogMask) ? DISABLED : PRIORITY;
	CPRINTS("ALS data logging now %sabled", gAlsLogMask ? "en" : "dis");
	if (argc > 1) {
		gAlsLogMask = atoi(argv[1]);
		CPRINTS("ALS data logging mask set to %d", gAlsLogMask);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(alslog, command_log_als_data,
	"",
	"Toggle state of ALS data logging.");

#define ALSLOG(level, format, args...) do { if (gAlsLogMask & level) \
					CPRINTS(format, ## args); \
				} while (0)
#else
#define ALSLOG(level, format, args...) { }
#endif

#ifdef CONFIG_ACCEL_FIFO
static volatile uint32_t last_interrupt_timestamp;
#endif

static inline int tcs3400_i2c_read8(const struct motion_sensor_t *s,
				    int reg, int *data)
{
	return i2c_read8(s->port, s->addr, reg, data);
}

static inline int tcs3400_i2c_write8(const struct motion_sensor_t *s,
				     int reg, int data)
{
	return i2c_write8(s->port, s->addr, reg, data);
}

static int tcs3400_read(const struct motion_sensor_t *s, intv3_t v)
{
	int ret;

	/* Enable power, ADC, and interrupt to start cycle */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_COLLECTING);

	/*
	 * If write succeeded, we've started the read process, but can't
	 * complete it yet until data is ready, so pass back EC_RES_IN_PROGRESS
	 * to inform upper level that read data process is under way and data
	 * will be delivered when available.
	 */
	if (ret == EC_SUCCESS)
		ret = EC_RES_IN_PROGRESS;

	return ret;
}

static int tcs3400_rgb_read(const struct motion_sensor_t *s, intv3_t v)
{
	return EC_SUCCESS;
}

static int tcs3400_get_saturation_point(struct tcs_saturation_t *data)
{
	/* Calculation from TCS3400 data sheet */
	return MIN((256 - data->atime) * 1024, 65535);
}

/*
 * tcs3400_adjust_sensor_for_saturation() tries to keep RGBC values as
 * close to saturation as possible. implementing the following logic:
 *
 * Initialize ATIME and AGAIN:
 *     ATIME = 700ms (0x10)
 *     AGAIN = 16x (0x02)
 *
 * If any of the R, G, B, or C channels have saturated ( value > 90%
 * of max value ), then decrease AGAIN. If AGAIN is already at its
 * minimum and can't be decreased, increase ATIME if it is not at
 * its maximum already.
 *
 * Else if none of the R, G, B, and C channels have saturated,
 * then increase AGAIN if it is not already at its maximum, and
 * if it is, decrease ATIME if it is not at it's minimum already.
 */
static int
tcs3400_adjust_sensor_for_saturation(struct motion_sensor_t *s,
				     uint16_t *crgb_data)
{
	struct tcs_saturation_t *sat_p =
			&TCS3400_RGB_DRV_DATA(s+1)->saturation;
	uint8_t save_again = sat_p->again;
	uint8_t save_atime = sat_p->atime;
	uint16_t saturation_level = tcs3400_get_saturation_point(sat_p);
	int ret = EC_SUCCESS;
	int status = 0;

	/* Adjust for saturation if needed */
	ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &status);
	if (ret)
		return ret;

	ALSLOG(SATURATION, "Saturation: level=0x%x, 90%=0x%x : "
		"0x%x 0x%x 0x%x 0x%x",
		saturation_level, saturation_level*90/100, crgb_data[0],
		crgb_data[1], crgb_data[2], crgb_data[3]);

	if ((status & TCS_I2C_STATUS_ALS_VALID) ||
	    (crgb_data[0] >= saturation_level) ||
	    (crgb_data[1] >= saturation_level) ||
	    (crgb_data[2] >= saturation_level) ||
	    (crgb_data[3] >= saturation_level)) {
		/* Saturation occurred, decrease AGAIN if we can */
		ALSLOG(SATURATION, "Saturation, looking to decrease again");
		if (sat_p->again > TCS_MIN_AGAIN)
			sat_p->again--;
		else if (sat_p->atime < TCS_MAX_ATIME) {
			/* reduce ATIME by incrementing atime register */
			sat_p->atime++;
		}
	} else {
		/* If value < 90% of saturation, try to increase sensitivity */
		saturation_level = saturation_level * 90 / 100;
		if ((crgb_data[0] < saturation_level) &&
		    (crgb_data[1] < saturation_level) &&
		    (crgb_data[2] < saturation_level) &&
		    (crgb_data[3] < saturation_level)) {
			/* increase AGAIN if we can */
			ALSLOG(SATURATION, "look to increase min saturation "
				"level=0x%x : 0x%x 0x%x 0x%x 0x%x",
				saturation_level, crgb_data[0], crgb_data[1],
				crgb_data[2], crgb_data[3]);

			if (sat_p->again < TCS_MAX_AGAIN) {
				sat_p->again++;
			} else if (sat_p->atime > TCS_MIN_ATIME) {
				/* increase ATIME */
				sat_p->atime--;
			}
		} else {
			ALSLOG(SATURATION, "saturation sweet spot (0x%08x < "
			       "0x%08x, sat = 0x%08x)", crgb_data[0],
			       saturation_level, saturation_level*100/90);
		}
	}

	/* If atime or gain setting changed, update atime and gain registers */
	if (save_again != sat_p->again) {
		ret = tcs3400_i2c_write8(s, TCS_I2C_CONTROL,
				(sat_p->again & TCS_I2C_CONTROL_MASK));
		if (ret)
			return ret;
		ALSLOG(SATURATION, "Set AGAIN = 0x%02x", sat_p->again);
	}

	if (save_atime != sat_p->atime) {
		ret = tcs3400_i2c_write8(s, TCS_I2C_CONTROL, sat_p->atime);
		if (ret)
			return ret;
		ALSLOG(SATURATION, "Set ATIME = 0x%02x", sat_p->atime);
	}

	return ret;
}

/**
 * normalize_channel_data - normalize the light data to remove effect of
 * different atime and again settings from the sample.
 */
static int normalize_channel_data(struct motion_sensor_t *s, int sample)
{
	struct tcs_saturation_t *sat_p =
				&(TCS3400_RGB_DRV_DATA(s+1)->saturation);
	/* index of value = AGAIN register setting (eg. AGAIN=2 for 16x) */
	const int tcs3400_agains[] = { 1, 4, 16, 64 };
	const int cur_gain = tcs3400_agains[sat_p->again];
	const int max_gain = 64;
	const int max_atime = 256;

	return(DIV_ROUND_NEAREST(sample * max_atime * max_gain,
				(max_atime - sat_p->atime) * cur_gain));
}


static void tcs3400_translate_to_xyz(struct motion_sensor_t *s,
				     int32_t *crgb_data, int32_t *xyz_data)
{
	struct tcs3400_rgb_drv_data_t *rgb_drv_data = TCS3400_RGB_DRV_DATA(s+1);
	int32_t crgb_prime[4];
	int32_t IR;
	int i;

	ALSLOG(VERBOSE, "received crbg_data [ 0x%04x 0x%04x, 0x%04x, 0x%04x ]",
	       crgb_data[0], crgb_data[1], crgb_data[2], crgb_data[3]);

	/* IR removal */
	IR = (crgb_data[1] + crgb_data[2] + crgb_data[3] - crgb_data[0]) / 2;

	ALSLOG(VERBOSE, "IR = %d + %d + %d - %d / 2 = %d (0x%04x + 0x%04x"
			"+ 0x%04x - 0x%04x) = 0x%08x", crgb_data[1],
			crgb_data[2], crgb_data[2], crgb_data[0], IR,
			crgb_data[1], crgb_data[2], crgb_data[3],
			crgb_data[0], IR);

	for (i = 0; i < ARRAY_SIZE(crgb_prime); i++) {
		if (crgb_data[i] < IR) {
			ALSLOG(ERRORS, "ERROR - IR > crgb_data[i] (0x%08x > "
				"0x%08x)", IR, crgb_data[i]+IR);
			crgb_prime[i] = 0;
		} else {
			crgb_prime[i] = crgb_data[i] - IR;
		}
	}

	/* regression fit to XYZ space */
	for (i = 0; i < 3; i++) {
		const struct rgb_calibration_t *p = &rgb_drv_data->rgb_cal[i];
		ALSLOG(VERBOSE, "coeff[%d] = { %d.%d, %d.%d, %d.%d, %d.%d }",
			i, p->coeff[RED_IDX]>>16, p->coeff[RED_IDX]&0xffff,
			p->coeff[GREEN_IDX]>>16, p->coeff[GREEN_IDX]&0xffff,
			p->coeff[BLUE_IDX]>>16, p->coeff[BLUE_IDX]&0xffff,
			p->coeff[CLEAR_IDX]>>16, p->coeff[CLEAR_IDX]&0xffff);

		xyz_data[i] = p->offset +
			fp_mul(p->coeff[RED_IDX], crgb_prime[RED_IDX]) +
			fp_mul(p->coeff[GREEN_IDX], crgb_prime[GREEN_IDX]) +
			fp_mul(p->coeff[BLUE_IDX], crgb_prime[BLUE_IDX]) +
			fp_mul(p->coeff[CLEAR_IDX], crgb_prime[CLEAR_IDX]);

		ALSLOG(VERBOSE, "xyz_data[%d] = %d + (%d * %d.%d) + "
			"(%d * %d.%d) + (%d * %d.%d) + (%d * %d.%d)",
			i, p->offset, crgb_prime[RED_IDX],
			p->coeff[RED_IDX]>>16, p->coeff[RED_IDX]&0xffff,
			crgb_prime[GREEN_IDX], p->coeff[GREEN_IDX]>>16,
			p->coeff[GREEN_IDX]&0xffff, crgb_prime[BLUE_IDX],
			p->coeff[BLUE_IDX]>>16, p->coeff[BLUE_IDX]&0xffff,
			crgb_prime[CLEAR_IDX], p->coeff[CLEAR_IDX]>>16,
			p->coeff[CLEAR_IDX]&0xffff);
		ALSLOG(VERBOSE, "xyz_data[%d] = %d.%d", i,
			xyz_data[i]>>16, xyz_data[i]&0xffff);

		if (xyz_data[i] < 0) {
			ALSLOG(ERRORS, "ERROR - xyz_data[i] 0%08x negative, "
				"setting to 0", xyz_data[i]);
			xyz_data[i] = 0;
		}

	}
}

static void tcs3400_process_raw_data(struct motion_sensor_t *s,
				    uint8_t *raw_data_buf,
				    uint16_t *raw_light_data, fp_t *xyz_data)
{
	struct motion_sensor_t *rgb_s = s + 1;  /* rgb driver always at s+1 */
	uint8_t calibration_mode =
			TCS3400_RGB_DRV_DATA(rgb_s)->calibration_mode;
	int32_t crgb_data[4];
	int32_t channel_scale = 1; /* 1x scale for clear channel */
	int32_t device_scale = TCS3400_DRV_DATA(s)->als_cal.scale;
	int32_t device_uscale = TCS3400_DRV_DATA(s)->als_cal.uscale;
	int i;
#ifdef CONFIG_CMD_ALSLOG
	int32_t old;
#endif

	/* adjust for calibration and scale data */
	for (i = 0; i < 4; i++) {
		int index = i * 2;

		/* assemble the light value for this channel */
		crgb_data[i] = raw_light_data[i] =
			((raw_data_buf[index+1] << 8) | raw_data_buf[index]);
		ALSLOG(RAW_DATA, "raw: crbg_data[%d] = %d (0x%04x)",
		       i, crgb_data[i], crgb_data[i]);

		/* in calibration mode, we only assemble the raw data */
		if (calibration_mode)
			continue;

		/* rgb data at index 1, 2, and 3 owned by rgb driver, not ALS */
		if (i > 0) {
			channel_scale =
				TCS3400_RGB_DRV_DATA(rgb_s)->rgb_cal[i-1].scale;
			/* convert scale format */
			channel_scale = ALS_APPLY_CHANNEL_SCALE(channel_scale);
			device_scale =
				TCS3400_RGB_DRV_DATA(rgb_s)->device_scale;
			device_uscale =
				TCS3400_RGB_DRV_DATA(rgb_s)->device_uscale;
			ALSLOG(VERBOSE, "channel_scale = %d, device_scale = %d,"
			       "uscale = %d", channel_scale, device_scale,
			       device_uscale);
		}

		/* multiply by individual channel scale value (val 0..2) */
		crgb_data[i] *= channel_scale;
		ALSLOG(VERBOSE, "scale: crbg_data[%d] *= %d = %d (0x%04x)",
		       i, channel_scale, crgb_data[i], crgb_data[i]);

		/* compensate for the light cover */
		#ifdef CONFIG_CMD_ALSLOG
		old = crgb_data[i];
		#endif
		crgb_data[i] = crgb_data[i] * device_scale +
				crgb_data[i] * device_uscale / 10000;

		ALSLOG(VERBOSE, "cover: rbg_data[%d] = %d (0x%04x) * %d + %d "
			"(0x%04x) * %d / 1000 = %d (0x%x)", i, old, old,
			device_scale, old, old, device_uscale, crgb_data[i],
			crgb_data[i]);

		/* normalize the data for atime and again changes */
		crgb_data[i] = normalize_channel_data(s, crgb_data[i]);
	}

	if (calibration_mode == 0) {
		ALSLOG(VERBOSE, "passing crbg_data[ 0x%04x, 0x%04x, 0x%04x, "
			"0x%04x ]", crgb_data[0], crgb_data[1], crgb_data[2],
			crgb_data[3]);
		tcs3400_translate_to_xyz(s, crgb_data, xyz_data);
		ALSLOG(VERBOSE, "back with [ 0x%04x, 0x%04x, 0x%04x ]",
			xyz_data[0], xyz_data[1], xyz_data[2]);
	} else {
		/* calibration mode returns raw data */
		for (i = 0; i < 3; i++)
			xyz_data[i] = crgb_data[i+1];
	}
}

static int tcs3400_post_events(struct motion_sensor_t *s, uint32_t last_ts)
{
	/*
	 * Rule says RGB sensor is right after ALS sensor.
	 * This routine will only get called from ALS sensor driver.
	 */
	struct motion_sensor_t *rgb_s = s + 1;
	uint8_t calibration_mode =
			TCS3400_RGB_DRV_DATA(rgb_s)->calibration_mode;
	struct ec_response_motion_sensor_data vector;
	uint8_t buf[TCS_RGBC_DATA_SIZE]; /* holds raw data read from chip */
	int32_t xyz_data[3] = { 0, 0, 0 };
	uint16_t raw_data[4]; /* holds raw CRGB data assembled from buf[] */
	int retries = 20;     /* 400 ms max */
	int *last_v = s->raw_xyz;
	int32_t data = 0;
	int i, ret = EC_SUCCESS;

	/* Make sure data is valid */
	do {
		ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &data);
		if (ret)
			return ret;
		if (!(data & TCS_I2C_STATUS_RGBC_VALID)) {
			retries--;
			if (retries == 0)
				return EC_ERROR_UNCHANGED;
			CPRINTS("RGBC not valid (0x%x)", data);
			msleep(20);
		}
	} while (!(data & TCS_I2C_STATUS_RGBC_VALID));

	/* Read the light registers */
	ret = i2c_read_block(s->port, s->addr, TCS_DATA_START_LOCATION,
			buf, sizeof(buf));
	if (ret)
		return ret;

	/* Process the raw light data, adjusting for scale and calibration */
	tcs3400_process_raw_data(s, buf, raw_data, xyz_data);

	/* if clear channel data changed, send illuminance upstream */
	if (last_v[X] != xyz_data[Y]) {
		if (calibration_mode)
			last_v[X] = raw_data[CLEAR_IDX];
		else
			last_v[X] = xyz_data[Y];
		vector.flags = 0;
		vector.data[X] = last_v[X];
		vector.data[Y] = last_v[Y] = 0;
		vector.data[Z] = last_v[Z] = 0;

#ifdef CONFIG_ACCEL_SPOOF_MODE
		/* If in spoof mode, replace actual data with our fake data */
		if (s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE) {
			for (i = 0; i < 3; i++)
				vector.data[i] = last_v[i] = s->spoof_xyz[i];
		}
#endif /* CONFIG_ACCEL_SPOOF_MODE */

#ifdef CONFIG_ACCEL_FIFO
		vector.sensor_num = s - motion_sensors;
		ALSLOG(PRIORITY, "Sending clear channel data [0x%04x]",
		       (unsigned short) vector.data[X]);
		motion_sense_fifo_add_data(&vector, s, 3, last_ts);
#endif
	} else {
		ALSLOG(PRIORITY, "Clear channel data unchanged [0x%04x]",
		       (unsigned short) xyz_data[Y]);
	}

	/* if rgb channel data changed since last sample, send it upstream */
	last_v = rgb_s->raw_xyz;
	if ((last_v[X] != xyz_data[X]) || (last_v[Y] != xyz_data[Y]) ||
		(last_v[Z] != xyz_data[Z])) {
		vector.flags = 0;
		if (calibration_mode) {
			for (i = 0; i < 3; i++)
				vector.data[i] = last_v[i] = raw_data[i+1];
		} else {
			for (i = 0; i < 3; i++)
				vector.data[i] = last_v[i] = xyz_data[i];
		}
#ifdef CONFIG_ACCEL_SPOOF_MODE
		if (rgb_s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE) {
			for (i = 0; i < 3; i++) {
				vector.data[i] = last_v[i] =
						rgb_s->spoof_xyz[i];
			}
		}
#endif /* CONFIG_ACCEL_SPOOF_MODE */

#ifdef CONFIG_ACCEL_FIFO
		ALSLOG(PRIORITY, "Sending RGB XYZ data [0x%04x 0x%04x 0x%04x]",
			(unsigned short) vector.data[X],
			(unsigned short) vector.data[Y],
			(unsigned short) vector.data[Z]);
		vector.sensor_num = rgb_s - motion_sensors;
		motion_sense_fifo_add_data(&vector, rgb_s, 3, last_ts);
#endif
	} else {
		ALSLOG(PRIORITY, "RGB channel unchanged [0x%04x 0x%04x 0x%04x]",
			(unsigned short) xyz_data[X],
			(unsigned short) xyz_data[Y],
			(unsigned short) xyz_data[Z]);
	}

	if (calibration_mode == 0)
		ret = tcs3400_adjust_sensor_for_saturation(s, raw_data);
	return ret;
}

void tcs3400_interrupt(enum gpio_signal signal)
{
#ifdef CONFIG_ACCEL_FIFO
	last_interrupt_timestamp = __hw_clock_source_read();
#endif
	task_set_event(TASK_ID_MOTIONSENSE,
		       CONFIG_ALS_TCS3400_INT_EVENT, 0);
}

/*
 * tcs3400_irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt,
 * and posts those events via motion_sense_fifo_add_data().
 *
 * This routine will get called for the TCS3400 ALS driver, but NOT for the
 * RGB driver.  We harvest data for both drivers in this routine.  The RGB
 * driver is guaranteed to directly follow the ALS driver in the sensor list
 * (i.e rgb's motion_sensor_t structure can be found at (s+1),
 */
static int tcs3400_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int status = 0;
	int ret = EC_SUCCESS;

	if (!(*event & CONFIG_ALS_TCS3400_INT_EVENT))
		return EC_ERROR_NOT_HANDLED;

	ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &status);
	if (ret)
		return ret;

	ALSLOG(DEBUG, "status=0x%x", status);

	/* Disable future interrupts */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_IDLE);
	if (ret)
		return ret;

	if ((status & TCS_I2C_STATUS_RGBC_VALID) ||
		((status & TCS_I2C_STATUS_ALS_IRQ) &&
		(status & TCS_I2C_STATUS_ALS_VALID))) {
		ret = tcs3400_post_events(s, last_interrupt_timestamp);
		if (ret)
			return ret;
	}

	tcs3400_i2c_write8(s, TCS_I2C_AICLEAR, 0);

	/* Disable ADC and turn off internal oscillator */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_SUSPEND);
	if (ret)
		return ret;

	return ret;
}

static int tcs3400_rgb_get_range(const struct motion_sensor_t *s)
{
	/* Currently, calibration info is same for all channels */
	return (TCS3400_RGB_DRV_DATA(s)->device_scale << 16) |
			TCS3400_RGB_DRV_DATA(s)->device_uscale;
}

static int tcs3400_rgb_set_range(const struct motion_sensor_t *s,
				 int range,
				 int rnd)
{
	TCS3400_RGB_DRV_DATA(s)->device_scale = range >> 16;
	TCS3400_RGB_DRV_DATA(s)->device_uscale = range & 0xffff;
	return EC_SUCCESS;
}

static int tcs3400_rgb_get_scale(const struct motion_sensor_t *s,
				 uint16_t *scale,
				 int16_t *temp)
{
	scale[X] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[X].scale;
	scale[Y] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[Y].scale;
	scale[Z] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[Z].scale;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_rgb_set_scale(const struct motion_sensor_t *s,
				 const uint16_t *scale,
				 int16_t temp)
{
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[X].scale = scale[X];
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[Y].scale = scale[Y];
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[Z].scale = scale[Z];
	return EC_SUCCESS;
}

static int tcs3400_rgb_get_offset(const struct motion_sensor_t *s,
				  int16_t *offset,
				  int16_t *temp)
{
	offset[X] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[X].offset;
	offset[Y] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[Y].offset;
	offset[Z] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[Z].offset;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_rgb_set_offset(const struct motion_sensor_t *s,
				  const int16_t *offset,
				  int16_t temp)
{
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[X].offset = offset[X];
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[Y].offset = offset[Y];
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[Z].offset = offset[Z];
	return EC_SUCCESS;
}

static int tcs3400_rgb_get_data_rate(const struct motion_sensor_t *s)
{
	return TCS3400_RGB_DRV_DATA(s)->rate;
}

static int tcs3400_rgb_set_data_rate(const struct motion_sensor_t *s,
				     int rate,
				     int rnd)
{
	TCS3400_RGB_DRV_DATA(s)->rate = rate;
	return EC_SUCCESS;
}

/* Set driver into special factory calibration mode */
static int tcs3400_perform_calib(const struct motion_sensor_t *s)
{
	int ret;
	struct tcs_saturation_t *sat_p =
			&TCS3400_RGB_DRV_DATA(s+1)->saturation;

	/* Let driver know it's in calibration mode */
	TCS3400_RGB_DRV_DATA(s+1)->calibration_mode = 1;

	/* set atime and again up for calibration mode */
	if (sat_p->again != TCS_CALIBRATION_AGAIN) {
		sat_p->again = TCS_CALIBRATION_AGAIN;
		ret = tcs3400_i2c_write8(s, TCS_I2C_CONTROL,
				(sat_p->again & TCS_I2C_CONTROL_MASK));
		if (ret)
			return ret;
	}
	if (sat_p->atime != TCS_CALIBRATION_ATIME) {
		sat_p->atime = TCS_CALIBRATION_ATIME;
		ret = tcs3400_i2c_write8(s, TCS_I2C_CONTROL, sat_p->atime);
		if (ret)
			return ret;
	}
	return EC_SUCCESS;
}

static int tcs3400_get_range(const struct motion_sensor_t *s)
{
	return (TCS3400_DRV_DATA(s)->als_cal.scale << 16) |
			(TCS3400_DRV_DATA(s)->als_cal.uscale);
}

static int tcs3400_set_range(const struct motion_sensor_t *s,
			     int range,
			     int rnd)
{
	TCS3400_DRV_DATA(s)->als_cal.scale = range >> 16;
	TCS3400_DRV_DATA(s)->als_cal.uscale = range & 0xffff;
	return EC_SUCCESS;
}

static int tcs3400_get_offset(const struct motion_sensor_t *s,
			      int16_t *offset,
			      int16_t *temp)
{
	offset[X] = TCS3400_DRV_DATA(s)->als_cal.offset;
	offset[Y] = 0;
	offset[Z] = 0;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_set_offset(const struct motion_sensor_t *s,
			      const int16_t *offset,
			      int16_t temp)
{
	TCS3400_DRV_DATA(s)->als_cal.offset = offset[X];
	return EC_SUCCESS;
}

static int tcs3400_get_data_rate(const struct motion_sensor_t *s)
{
	return TCS3400_DRV_DATA(s)->rate;
}

static int tcs3400_set_data_rate(const struct motion_sensor_t *s,
				 int rate,
				 int rnd)
{
	enum tcs3400_mode mode;
	int data;
	int ret;

	if (rate == 0) {
		/* Suspend driver */
		mode = TCS3400_MODE_SUSPEND;
	} else {
		/*
		 * We set the sensor for continuous mode,
		 * integrating over 800ms.
		 * Do not allow range higher than 1Hz.
		 */
		if (rate > 1000)
			rate = 1000;
		mode = TCS3400_MODE_COLLECTING;
	}
	TCS3400_DRV_DATA(s)->rate = rate;

	ret = tcs3400_i2c_read8(s, TCS_I2C_ENABLE, &data);
	if (ret)
		return ret;

	data = (data & TCS_I2C_ENABLE_MASK) | mode;
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, data);

	return ret;
}

/**
 * Initialise TCS3400 light sensor.
 */
static int tcs3400_rgb_init(const struct motion_sensor_t *s)
{
	return sensor_init_done(s);
}

static int tcs3400_init(const struct motion_sensor_t *s)
{
	/*
	 * These are default power-on register values with two exceptions:
	 * Set ATIME = 0 (712 ms)
	 * Set AGAIN = 16 (0x10)  (AGAIN is in CONTROL register)
	 */
	const struct reg_data {
		uint8_t reg;
		uint8_t data;
	} defaults[] = {
		{ TCS_I2C_ENABLE, 0 },
		{ TCS_I2C_ATIME, TCS_DEFAULT_ATIME },
		{ TCS_I2C_WTIME, 0xFF },
		{ TCS_I2C_AILTL, 0 },
		{ TCS_I2C_AILTH, 0 },
		{ TCS_I2C_AIHTL, 0 },
		{ TCS_I2C_AIHTH, 0 },
		{ TCS_I2C_PERS, 0 },
		{ TCS_I2C_CONFIG, 0x40 },
		{ TCS_I2C_CONTROL, (TCS_DEFAULT_AGAIN & TCS_I2C_CONTROL_MASK)},
		{ TCS_I2C_AUX, 0 },
		{ TCS_I2C_IR, 0 },
		{ TCS_I2C_CICLEAR, 0 },
		{ TCS_I2C_AICLEAR, 0 }
	};
	int data = 0;
	int ret;

	ret = tcs3400_i2c_read8(s, TCS_I2C_ID, &data);
	if (ret) {
		CPRINTS("failed reading ID reg 0x%x, ret=%d", TCS_I2C_ID, ret);
		return ret;
	}
	if ((data != TCS340015_DEVICE_ID) && (data != TCS340037_DEVICE_ID)) {
		CPRINTS("no ID match, data = 0x%x", data);
		return EC_ERROR_ACCESS_DENIED;
	}

	/* reset chip to default power-on settings, changes ATIME and CONTROL */
	for (int x = 0; x < ARRAY_SIZE(defaults); x++) {
		ret = tcs3400_i2c_write8(s, defaults[x].reg, defaults[x].data);
		if (ret)
			return ret;
	}

	return sensor_init_done(s);
}

const struct accelgyro_drv tcs3400_drv = {
	.init = tcs3400_init,
	.read = tcs3400_read,
	.set_range = tcs3400_set_range,
	.get_range = tcs3400_get_range,
	.set_offset = tcs3400_set_offset,
	.get_offset = tcs3400_get_offset,
	.set_data_rate = tcs3400_set_data_rate,
	.get_data_rate = tcs3400_get_data_rate,
	.perform_calib = tcs3400_perform_calib,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.irq_handler = tcs3400_irq_handler,
#endif
};

const struct accelgyro_drv tcs3400_rgb_drv = {
	.init = tcs3400_rgb_init,
	.read = tcs3400_rgb_read,
	.set_range = tcs3400_rgb_set_range,
	.get_range = tcs3400_rgb_get_range,
	.set_offset = tcs3400_rgb_set_offset,
	.get_offset = tcs3400_rgb_get_offset,
	.set_scale = tcs3400_rgb_set_scale,
	.get_scale = tcs3400_rgb_get_scale,
	.set_data_rate = tcs3400_rgb_set_data_rate,
	.get_data_rate = tcs3400_rgb_get_data_rate,
};
