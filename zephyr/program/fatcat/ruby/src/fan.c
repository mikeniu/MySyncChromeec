/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fan.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"

#define FAN_TEMPERATURE_WINDOW_SAMPLES 11

/* 11 one-second samples span a 10-second window, including both endpoints. */
static int temperature_samples[TEMP_SENSOR_COUNT]
			      [FAN_TEMPERATURE_WINDOW_SAMPLES];
static int temperature_sum[TEMP_SENSOR_COUNT];
static int sample_count;
static int next_sample_index;
static int applied_percent;
static int applied_percent_valid;

static void reset_fan_temperature_average(void)
{
	memset(temperature_samples, 0, sizeof(temperature_samples));
	memset(temperature_sum, 0, sizeof(temperature_sum));
	sample_count = 0;
	next_sample_index = 0;
}

/*
 * Keep the default thermal fan curve: use the greatest requested cooling
 * percentage among all sensors configured with temp-fan-off and
 * temp-fan-max in thermal.dtsi.
 */
static int get_fan_percent(const int *temp)
{
	int fmax = 0;
	int i;

	for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
		int percent;

		if (!thermal_params[i].temp_fan_off ||
		    !thermal_params[i].temp_fan_max)
			continue;

		percent = thermal_fan_percent(thermal_params[i].temp_fan_off,
					      thermal_params[i].temp_fan_max,
					      C_TO_K(temp[i]));
		if (percent > fmax)
			fmax = percent;
	}

	return fmax;
}

/*
 * board_override_fan_control() receives only temperatures, not the read
 * status for each sensor. Read the sensors used by the Ruby fan curve again
 * so a failed read cannot enter the moving-average window.
 */
static int read_fan_temperatures(int *temp)
{
	int i;

	for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
		int temp_k;

		if (!thermal_params[i].temp_fan_off ||
		    !thermal_params[i].temp_fan_max)
			continue;

		if (temp_sensor_read(i, &temp_k) != EC_SUCCESS) {
			reset_fan_temperature_average();
			return -1;
		}

		temp[i] = K_TO_C(temp_k);
	}

	return 0;
}

static int average_fan_temperature(const int *temp, int *average_temp)
{
	int i;

	for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
		if (!thermal_params[i].temp_fan_off ||
		    !thermal_params[i].temp_fan_max)
			continue;

		if (sample_count == FAN_TEMPERATURE_WINDOW_SAMPLES) {
			temperature_sum[i] -=
				temperature_samples[i][next_sample_index];
		}

		temperature_samples[i][next_sample_index] = temp[i];
		temperature_sum[i] += temp[i];
	}

	next_sample_index =
		(next_sample_index + 1) % FAN_TEMPERATURE_WINDOW_SAMPLES;

	if (sample_count < FAN_TEMPERATURE_WINDOW_SAMPLES)
		sample_count++;

	if (sample_count < FAN_TEMPERATURE_WINDOW_SAMPLES)
		return 0;

	for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
		if (!thermal_params[i].temp_fan_off ||
		    !thermal_params[i].temp_fan_max)
			continue;

		average_temp[i] = temperature_sum[i] / sample_count;
	}

	return 1;
}

void board_override_fan_control(int fan, int *temp)
{
	int current_temp[TEMP_SENSOR_COUNT] = { 0 };
	int average_temp[TEMP_SENSOR_COUNT] = { 0 };
	int average_ready;
	int percent;

	(void)temp;

	if (read_fan_temperatures(current_temp) < 0)
		return;

	average_ready = average_fan_temperature(current_temp, average_temp);

	if (!applied_percent_valid) {
		percent = get_fan_percent(current_temp);
		fan_set_percent_needed(fan, percent);
		applied_percent = percent;
		applied_percent_valid = 1;
	}

	if (!average_ready)
		return;

	percent = get_fan_percent(average_temp);
	/*
	 * A chipset reset or resume initializes the fan to the common
	 * CONFIG_FAN_INIT_SPEED. Reapply the Ruby target if that changed the
	 * target RPM, even when the averaged temperature percentage is
	 * unchanged.
	 */
	if (percent == applied_percent &&
	    fan_get_rpm_target(FAN_CH(fan)) == fan_percent_to_rpm(fan, percent))
		return;

	/* Preserve fan startup and RPM conversion behavior from common/fan.c.
	 */
	fan_set_percent_needed(fan, percent);
	applied_percent = percent;
	applied_percent_valid = 1;
}
