/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "body_detection.h"
#include "console.h"
#include "filter_iir.h"
#include "hooks.h"
#include "hwtimer.h"
#include "lid_switch.h"
#include "math_util.h"
#include "mkbp_input_devices.h"
#include "motion_sense_fifo.h"
#include "timer.h"

#define DEBUG

/* Console output macros */
#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)

/*
 * Filter parameters are calculated to work with 15Hz sampling frequency.
 * However, the range for which the algorithm provides reliable detection is
 * wider (min 10Hz, max 20Hz).
 */
#define ALGORITHM_FREQ_MIN 10
#define ALGORITHM_FREQ_OPTIMAL 15
#define ALGORITHM_FREQ_MAX 20
/*
 * How many samples have to be ignored for variance estimator
 * calculation after boot. We definitely don't want to change
 * default on-body state too early when filters and smoothers
 * do not contain valid data yet.
 * Use sample count for 5s period as default.
 */
#define INITIAL_INACTIVITY_SAMPLES (5 * ALGORITHM_FREQ_MAX)

#ifdef DEBUG
#define CPRINTS_DBG(fmt, args...) CPRINTS(fmt, ##args)
#else
#define CPRINTS_DBG(fmt, args...)
#endif

test_export_static float var_threshold;
test_export_static float confidence_delta;

test_export_static struct motion_sensor_t *body_sensor =
	&motion_sensors[CONFIG_BODY_DETECTION_SENSOR];

const static struct body_detect_params default_body_detect_params = {
	.var_threshold = CONFIG_BODY_DETECTION_VAR_THRESHOLD,
	.confidence_delta = CONFIG_BODY_DETECTION_CONFIDENCE_DELTA,
};

static bool body_detect_enable = IS_ENABLED(CONFIG_BODY_DETECTION);
static bool body_detect_initialized;
static bool body_detect_rate_is_set;
static enum body_detect_states motion_state = BODY_DETECTION_ON_BODY;
static uint64_t onbody_lasttime;
STATIC_IF(CONFIG_ACCEL_SPOOF_MODE) bool spoof_enable;
static struct filt_iir_t filt_main;
static struct filt_iir_t filt_dec_x, filt_dec_y, filt_dec_z;
static struct smooth_exp_t smooth_x, smooth_y, smooth_z, smooth_var;
static int dec_factor = 1;
static uint8_t dec_cnt;
static uint8_t dec_valid;
test_export_static timestamp_t (*get_time_ptr)() = get_time;

static uint64_t get_curtime(void)
{
	timestamp_t time = get_time_ptr();

	return time.val;
}

static void body_detect_get_params(void)
{
	/* If body detection params haven't been set, use the default ones. */
	if (!body_sensor->bd_params)
		body_sensor->bd_params = &default_body_detect_params;

	/*
	 * In case only some of the parameters have been specified use
	 * the default values for the rest of them.
	 */
	if (body_sensor->bd_params->var_threshold != 0)
		var_threshold = body_sensor->bd_params->var_threshold;
	else
		var_threshold = default_body_detect_params.var_threshold;

	if (body_sensor->bd_params->confidence_delta != 0)
		confidence_delta = body_sensor->bd_params->confidence_delta;
	else
		confidence_delta = default_body_detect_params.confidence_delta;

	CPRINTS_DBG("BD: var %d conf %d", (int)var_threshold,
		    (int)confidence_delta);
}

static void print_body_detect_mode(void)
{
	CPRINTS("body detect mode %sabled on %llu",
		body_detect_get_state() ? "en" : "dis", get_curtime());
}

/* Change the motion state and commit the change to AP. */
void body_detect_change_state(enum body_detect_states state, bool spoof)
{
	if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) && spoof_enable && !spoof)
		return;
	if (IS_ENABLED(CONFIG_GESTURE_HOST_DETECTION)) {
		struct ec_response_motion_sensor_data vector = {
			.flags = MOTIONSENSE_SENSOR_FLAG_BYPASS_FIFO,
			.activity_data = {
				.activity = MOTIONSENSE_ACTIVITY_BODY_DETECTION,
				.state = state,
			},
			.sensor_num = MOTION_SENSE_ACTIVITY_SENSOR_ID,
		};
		motion_sense_fifo_stage_data(&vector, NULL, 0,
					     __hw_clock_source_read());
		motion_sense_fifo_commit_data();
	}
	/* change the motion state */
	motion_state = state;
	if (state == BODY_DETECTION_ON_BODY) {
		/* reset time counting of stationary */
		onbody_lasttime = get_curtime();
	}

	/* state changing log */
	print_body_detect_mode();

	if (IS_ENABLED(CONFIG_BODY_DETECTION_NOTIFY_MODE_CHANGE) &&
	    motion_sense_get_ec_config() == SENSOR_CONFIG_EC_S0) {
		host_set_single_event(EC_HOST_EVENT_BODY_DETECT_CHANGE);
	}

	hook_notify(HOOK_BODY_DETECT_CHANGE);
}

void body_detect_set_enable(int enable)
{
	body_detect_enable = enable;
	body_detect_change_state(BODY_DETECTION_ON_BODY, false);
}

int body_detect_get_enable(void)
{
	return body_detect_enable;
}

enum body_detect_states body_detect_get_state(void)
{
	return motion_state;
}

void body_detect_set_update_rate(int odr)
{
	int dec_factor_tmp;
	int ret;

	if (odr == 0) {
		body_detect_rate_is_set = false;
		return;
	}

	dec_factor_tmp = odr / ALGORITHM_FREQ_OPTIMAL;
	dec_factor = (dec_factor_tmp + 500) / 1000;
	if (dec_factor == 0)
		dec_factor = 1;

	/*
	 * Initialize LPF filter.
	 * We expect dec_factor to be less than 10 which allows us
	 * to use lower rank of Butterworh filter. Rank 3 is enough here.
	 */
	ret = filter_butterworth_lpf_init(&filt_dec_x, 3, 1.0f / dec_factor);
	if (ret != 0) {
		CPRINTS("BD ERR: failed to initialize decimation filter, "
			"ret = %d",
			ret);
		return;
	}
	memcpy(&filt_dec_y, &filt_dec_x, sizeof(filt_dec_x));
	memcpy(&filt_dec_z, &filt_dec_x, sizeof(filt_dec_x));

	body_detect_get_params();

	dec_valid = 0;
	dec_cnt = 0;
	body_detect_rate_is_set = true;

#ifdef DEBUG
	CPRINTS("BD: ODR = %d, decimation N = %d", odr, dec_factor);
#endif
}

static void body_detect_init(float x0, float y0, float z0)
{
	int ret;

	/* Initialize X,Y,Z averaging filters */
	smooth_exp_init(&smooth_x, 0.95, x0);
	smooth_exp_init(&smooth_y, 0.95, y0);
	smooth_exp_init(&smooth_z, 0.95, z0);

	/*
	 * Initialize LPF (fcut = nyquist/15)
	 */
	ret = filter_butterworth_lpf_init(&filt_main, 5, 1.0f / 15.0f);
	if (ret != 0) {
		CPRINTS("BD ERR: failed to initialize IIR filter, ret = %d",
			ret);
		return;
	}

	/* Initialize Variance smoothing */
	smooth_exp_init(&smooth_var, 0.9, 0);

	body_detect_initialized = true;
}

test_export_static void body_detect_step(float x, float y, float z,
					 uint64_t curtime)
{
	static int inactivity_cnt;
	float x_avg = 0, y_avg = 0, z_avg = 0;
	float var = 0;
	float motion_confidence = 0;

	if (body_detect_initialized == false) {
		body_detect_init(x, y, z);
		body_detect_change_state(BODY_DETECTION_ON_BODY, false);
		return;
	}

	/* Do not process if rate is unknown */
	if (body_detect_rate_is_set == false) {
		return;
	}

	/* Check if we need to decimate */
	if (dec_factor > 1) {
		x = filter_iir_step(&filt_dec_x, x);
		y = filter_iir_step(&filt_dec_y, y);
		z = filter_iir_step(&filt_dec_z, z);

		dec_cnt++;

		/* No need to increment further and overflow accidentally */
		if (dec_valid < 128)
			dec_valid++;

		if (dec_cnt < dec_factor) {
			return;
		}

		dec_cnt = 0;
	}

	/* Wait INITIAL_INACTIVITY_SAMPLES before filter value is stable */
	if (dec_valid < INITIAL_INACTIVITY_SAMPLES) {
		return;
	}

	/* Smooth X, Y, Z data */
	x_avg = smooth_exp_step(&smooth_x, x);
	y_avg = smooth_exp_step(&smooth_y, y);
	z_avg = smooth_exp_step(&smooth_z, z);

	/*
	 * Ignore first INITIAL_INACTIVITY_SAMPLES to ensure all
	 * filters are up and running after the boot.
	 */
	if (inactivity_cnt < INITIAL_INACTIVITY_SAMPLES) {
		var = 0.0f;
		inactivity_cnt++;
	} else {
		/*
		 * Calculate scalar from vector.
		 *
		 * This function is not a proper metric, but has significant
		 * advantages over Euclidean vector length.
		 * 1. Average value in steady state is zero.
		 * 2. Functional is linear and symmetrical, so assuming the
		 * sensor noise is Gaussian then the noise cancels over some
		 * longer period of time (adding, smoothing); no need to
		 * calculate RMS noise and put it to the equation.
		 * 3. No need to store measurement history to calculate moving
		 * average or moving variance.
		 */
		var = (x - x_avg) + (y - y_avg) + (z - z_avg);

		/* Filter variance */
		var = filter_iir_step(&filt_main, var);
	}
	CPRINTS_DBG("var_raw=%d", (int)(var * 1000));

	/*
	 * Moving Variance estimator: use exponential smoothing
	 */
	var = smooth_exp_step(&smooth_var, var);
	if (var < 0.0f)
		var = -var;

	/*
	 * Calculate motion confidence in %, thresholds are in uGs but var
	 * is in mGs.
	 * confidence_delta defines a hysteresis range around var_threshold:
	 *      -------------------------------------------------------
	 *      | 100%      | var > var_threshold + confidence_delta  |
	 *      -------------------------------------------------------
	 *      | 0% - 100% | proporitonaly when var is between       |
	 *      -------------------------------------------------------
	 *      | 0%        | var < var_threshold - confidence_delta  |
	 *      -------------------------------------------------------
	 */
	var = var * 1000.0f;
	CPRINTS_DBG("var=%d", (int)var);
	if (var < var_threshold - confidence_delta) {
		motion_confidence = 0;
	} else if (var > var_threshold + confidence_delta) {
		motion_confidence = 100;
	} else {
		motion_confidence = 100.0f *
				    (var - var_threshold + confidence_delta) /
				    (2 * confidence_delta);
	}

	CPRINTS_DBG(
		"BD_DBG: t=%lld conf=%d var=%d x=%d/%d y=%d/%d z=%d/%d [%d %d %d]",
		curtime, (int)motion_confidence, (int)var, (int)x, (int)x_avg,
		(int)y, (int)y_avg, (int)z, (int)z_avg, motion_state,
		(int)var_threshold, (int)confidence_delta);

	/*
	 * Body detection
	 */
	if (motion_state == BODY_DETECTION_ON_BODY) {
		if (motion_confidence > CONFIG_BODY_DETECTION_ON_BODY_CON) {
			CPRINTS_DBG(
				"Confidence(%d) is above threshold(%d), updating timestamp to %llu",
				(int)motion_confidence,
				CONFIG_BODY_DETECTION_OFF_BODY_CON, curtime);
			onbody_lasttime = curtime;
		}
		if (curtime - onbody_lasttime >
		    (CONFIG_BODY_DETECTION_STATIONARY_DURATION * SEC_UL)) {
			CPRINTS_DBG("Going OFF body");
			body_detect_change_state(BODY_DETECTION_OFF_BODY,
						 false);
		} else {
			CPRINTS_DBG("Not enough time passed %llu - %llu > %lu",
				    curtime, onbody_lasttime,
				    (CONFIG_BODY_DETECTION_STATIONARY_DURATION *
				     SEC_UL));
		}
	} else {
		if (motion_confidence > CONFIG_BODY_DETECTION_ON_BODY_CON) {
			body_detect_change_state(BODY_DETECTION_ON_BODY, false);
		}
	}
}

void body_detect(void)
{
	if (!body_detect_enable)
		return;

	/*
	 * Motion sensor returns 16-bit RAW value with 1-bit sign. Convert
	 * to mG using formula:
	 *               value_RAW * range[G] * 1000
	 *   value[mG] = ---------------------------
	 *                          2^15
	 */
	body_detect_step(
		(body_sensor->xyz[X] * body_sensor->current_range * 1000) >> 15,
		(body_sensor->xyz[Y] * body_sensor->current_range * 1000) >> 15,
		(body_sensor->xyz[Z] * body_sensor->current_range * 1000) >> 15,
		get_curtime());
}

void body_detect_reset(void)
{
	int odr = body_sensor->drv->get_data_rate(body_sensor);

	if (motion_state == BODY_DETECTION_ON_BODY)
		onbody_lasttime = get_curtime();
	else
		body_detect_change_state(BODY_DETECTION_ON_BODY, spoof_enable);

	body_detect_get_params();
	body_detect_set_update_rate(odr);
}

#ifdef CONFIG_ACCEL_SPOOF_MODE
void body_detect_set_spoof(int enable)
{
	spoof_enable = enable;
	/* After disabling spoof mode, commit current state. */
	if (!enable)
		body_detect_change_state(motion_state, false);
}

bool body_detect_get_spoof(void)
{
	return spoof_enable;
}

static int command_setbodydetectionmode(int argc, const char **argv)
{
	if (argc == 1) {
		print_body_detect_mode();
		return EC_SUCCESS;
	}

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	/* |+1| to also make sure the strings the same length. */
	if (strncmp(argv[1], "on", strlen("on") + 1) == 0) {
		body_detect_change_state(BODY_DETECTION_ON_BODY, true);
		spoof_enable = true;
	} else if (strncmp(argv[1], "off", strlen("off") + 1) == 0) {
		body_detect_change_state(BODY_DETECTION_OFF_BODY, true);
		spoof_enable = true;
	} else if (strncmp(argv[1], "reset", strlen("reset") + 1) == 0) {
		body_detect_reset();
		/*
		 * Don't call body_detect_set_spoof(), since
		 * body_detect_change_state() was already called by
		 * body_detect_reset().
		 */
		spoof_enable = false;
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(
	bodydetectmode, command_setbodydetectionmode, "[on | off | reset]",
	"Manually force body detect mode to on (body), off (body) or reset.");
#endif /* CONFIG_ACCEL_SPOOF_MODE */
