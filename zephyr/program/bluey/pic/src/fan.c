/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pic Fan configuration */

#include "chipset.h"
#include "common.h"
#include "fan.h"
#include "hooks.h"
#include "power/qcom.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "timer.h"
#include "util.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pic_fan, LOG_LEVEL_INF);

#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_cpu))

/* Fan remains off (slop0) while CPU VR / EC_TEMP0 is below 40C. */
#define FAN_IDLE_OFF_C 40

/* Control uses a 10s average (HOOK_SECOND). */
#define TEMP_AVG_WINDOW 10

/* 10s missing samples -> max RPM; another 10s -> shutdown. */
#define SENSOR_FAIL_MAX_FAN_S 10
#define SENSOR_FAIL_SHUTDOWN_S 20

/* Treat RPM as in-band within ±100. */
#define FAN_RPM_ACCURACY 100

/* Duty ramp, applied once per second. */
#define DUTY_STEP_UP 1
#define DUTY_STEP_DOWN 2

static int current_level;
static int prev_current_level;
static int sensor_detect_fail_count;
static bool force_max_rpm;
static bool have_avg;
static int last_avg_c;

static int temp_hist[TEMP_AVG_WINDOW];
static uint8_t temp_hist_idx;
static uint8_t temp_hist_count;

static timestamp_t next_duty_adjust;

struct fan_step {
	int8_t on[1];
	int8_t off[1];
	uint16_t rpm[FAN_CH_COUNT];
};

#define FAN_TABLE_ENTRY(nd)                     \
	{                                       \
		.on = DT_PROP(nd, temp_on),     \
		.off = DT_PROP(nd, temp_off),   \
		.rpm = DT_PROP(nd, rpm_target), \
	},

static const struct fan_step fan_step_table[] = { DT_FOREACH_CHILD(
	DT_NODELABEL(fan_steps), FAN_TABLE_ENTRY) };

static void temp_avg_reset(void)
{
	temp_hist_idx = 0;
	temp_hist_count = 0;
	have_avg = false;
	last_avg_c = 0;
}

static int temp_cpu_avg(int sample_c)
{
	int sum = 0;
	int i;

	temp_hist[temp_hist_idx] = sample_c;
	temp_hist_idx = (temp_hist_idx + 1) % TEMP_AVG_WINDOW;
	if (temp_hist_count < TEMP_AVG_WINDOW)
		temp_hist_count++;

	for (i = 0; i < temp_hist_count; i++)
		sum += temp_hist[i];

	return sum / temp_hist_count;
}

static uint16_t fan_max_rpm(int fan)
{
	return fan_step_table[ARRAY_SIZE(fan_step_table) - 1].rpm[fan];
}

int fan_table_to_rpm(int fan, int *temp)
{
	int t_avg = temp[TEMP_CPU];

	/* slop0 while below idle-off threshold. */
	if (t_avg < FAN_IDLE_OFF_C) {
		current_level = 0;
	} else {
		/*
		 * Step up when the 10s average reaches High Temp of this slop.
		 * Step down when it meets Low Temp of this slop.
		 */
		while (current_level < ARRAY_SIZE(fan_step_table) - 1 &&
		       t_avg >= fan_step_table[current_level].on[TEMP_CPU])
			current_level++;

		while (current_level > 0 &&
		       t_avg <= fan_step_table[current_level].off[TEMP_CPU])
			current_level--;
	}

	if (current_level != prev_current_level)
		LOG_INF("Fan current_level: %d (avg %dC)", current_level,
			t_avg);

	prev_current_level = current_level;

	return fan_step_table[current_level].rpm[fan];
}

static enum fan_status fan_duty_status(int duty, int rpm_actual, int rpm_target)
{
	int rpm_diff = rpm_target - rpm_actual;

	if (duty == 0 && rpm_target == 0)
		return FAN_STATUS_STOPPED;
	if (rpm_target == 0)
		return FAN_STATUS_CHANGING;
	if (rpm_diff > FAN_RPM_ACCURACY) {
		if (duty >= 100)
			return FAN_STATUS_FRUSTRATED;
		return FAN_STATUS_CHANGING;
	}
	if (rpm_diff < -FAN_RPM_ACCURACY) {
		if (duty <= 1)
			return FAN_STATUS_FRUSTRATED;
		return FAN_STATUS_CHANGING;
	}
	return FAN_STATUS_LOCKED;
}

/**
 * Override default fan control.
 *
 * On Pic, the TACH signal's pull-up rail (+3V_VREG_MISC)
 * takes time to stabilize after AP power-on. During this
 * time, the TACH reports 0 RPM.
 *
 * Duty is ramped 1%/s while speeding up and 2%/s while slowing
 * down (Optimized Mode note 4).
 */
enum fan_status board_override_fan_control_duty(int ch)
{
	struct fan_data *data = &fan_data[ch];
	enum power_on_event_t power_on_reason;
	int duty;
	int rpm_diff;
	int rpm_actual;
	int rpm_target;

	power_on_reason = chipset_get_power_on_reason();

	/* If the last power-on is due to AC or RTC, which enters the charging
	 * loop in the AP firmware stop the fan */
	if (POWER_ON_BY_AC_ON == power_on_reason ||
	    POWER_ON_BY_RTC_ALARM == power_on_reason) {
		fan_set_duty(ch, 0);
		return FAN_STATUS_STOPPED;
	}

	rpm_actual = data->rpm_actual;
	rpm_target = data->rpm_target;
	duty = fan_get_duty(ch);

	if (!timestamp_expired(next_duty_adjust, NULL))
		return fan_duty_status(duty, rpm_actual, rpm_target);

	next_duty_adjust.val = get_time().val + USEC_PER_SEC;

	if (duty == 0 && rpm_target == 0)
		return FAN_STATUS_STOPPED;

	rpm_diff = rpm_target - rpm_actual;

	if (rpm_target == 0) {
		if (duty > DUTY_STEP_DOWN)
			duty -= DUTY_STEP_DOWN;
		else
			duty = 0;
		fan_set_duty(ch, duty);
		return duty == 0 ? FAN_STATUS_STOPPED : FAN_STATUS_CHANGING;
	}

	if (rpm_diff > FAN_RPM_ACCURACY) {
		if (duty >= 100)
			return FAN_STATUS_FRUSTRATED;
		if (duty + DUTY_STEP_UP > 100)
			duty = 100;
		else
			duty += DUTY_STEP_UP;
		fan_set_duty(ch, duty);
		return FAN_STATUS_CHANGING;
	}
	if (rpm_diff < -FAN_RPM_ACCURACY) {
		if (duty <= 1)
			return FAN_STATUS_FRUSTRATED;
		if (duty > DUTY_STEP_DOWN + 1)
			duty -= DUTY_STEP_DOWN;
		else
			duty = 1;
		fan_set_duty(ch, duty);
		return FAN_STATUS_CHANGING;
	}

	return FAN_STATUS_LOCKED;
}

void board_override_fan_control(int fan, int *temp)
{
	int t, rv;
	int target_rpm;

	/*
	 * In common/fan.c pwm_fan_stop() will turn off fan
	 * when chipset suspend or shutdown.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON)) {
		sensor_detect_fail_count = 0;
		force_max_rpm = false;
		temp_avg_reset();
		return;
	}

	rv = temp_sensor_read(TEMP_CPU, &t);
	if (rv != EC_SUCCESS) {
		if (sensor_detect_fail_count < SENSOR_FAIL_SHUTDOWN_S)
			sensor_detect_fail_count++;

		if (sensor_detect_fail_count == SENSOR_FAIL_MAX_FAN_S) {
			force_max_rpm = true;
			LOG_WRN("EC_TEMP0 missing %ds, fan -> max RPM",
				SENSOR_FAIL_MAX_FAN_S);
		}
		if (sensor_detect_fail_count >= SENSOR_FAIL_SHUTDOWN_S) {
			chipset_force_shutdown(CHIPSET_SHUTDOWN_THERMAL);
			LOG_ERR("EC_TEMP0 missing %ds, force shutdown",
				SENSOR_FAIL_SHUTDOWN_S);
		}
	} else {
		sensor_detect_fail_count = 0;
		force_max_rpm = false;
		last_avg_c = temp_cpu_avg(K_TO_C(t));
		have_avg = true;
		temp[TEMP_CPU] = last_avg_c;
	}

	if (force_max_rpm) {
		target_rpm = fan_max_rpm(fan);
	} else if (have_avg) {
		temp[TEMP_CPU] = last_avg_c;
		target_rpm = fan_table_to_rpm(fan, temp);
	} else {
		target_rpm = 0;
	}

	fan_set_rpm_mode(fan, true);
	fan_set_rpm_target(fan, target_rpm);
}

/* Trigger Zoss coverage check */