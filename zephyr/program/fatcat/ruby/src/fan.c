/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fan.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ruby_fan, LOG_LEVEL_DBG);

#define FAN_TEMPERATURE_STABLE_CYCLES 5

/* Ruby has one fan channel, so the temperature stability state is singular. */
static int previous_temp[TEMP_SENSOR_COUNT];
static int stable_cycles;
static int temperature_snapshot_valid;
static int applied_percent;

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

static int fan_temperature_stable(int fan, const int *temp)
{
	int changed = 0;
	int i;

	for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
		if (!thermal_params[i].temp_fan_off ||
		    !thermal_params[i].temp_fan_max)
			continue;

		if (!temperature_snapshot_valid)
			LOG_INF("F%d S%d: %dC, range %d-%dC", fan, i, temp[i],
				K_TO_C(thermal_params[i].temp_fan_off),
				K_TO_C(thermal_params[i].temp_fan_max));

		if (temperature_snapshot_valid && temp[i] != previous_temp[i]) {
			LOG_INF("F%d S%d: %dC -> %dC, reset", fan, i,
				previous_temp[i], temp[i]);
			changed = 1;
		}

		previous_temp[i] = temp[i];
	}

	if (!temperature_snapshot_valid) {
		temperature_snapshot_valid = 1;
		stable_cycles = 1;
		return FAN_TEMPERATURE_STABLE_CYCLES == 1;
	}

	if (changed) {
		stable_cycles = 1;
		return FAN_TEMPERATURE_STABLE_CYCLES == 1;
	}

	if (stable_cycles < FAN_TEMPERATURE_STABLE_CYCLES) {
		stable_cycles++;

		if (stable_cycles < FAN_TEMPERATURE_STABLE_CYCLES) {
			LOG_DBG("F%d stable %d/%d", fan, stable_cycles,
				FAN_TEMPERATURE_STABLE_CYCLES);
		} else {
			LOG_INF("F%d stable %d cycles, update", fan,
				stable_cycles);
		}
	}

	return stable_cycles >= FAN_TEMPERATURE_STABLE_CYCLES;
}

void board_override_fan_control(int fan, int *temp)
{
	int percent;

	if (!fan_temperature_stable(fan, temp))
		return;

	percent = get_fan_percent(temp);
	if (percent == applied_percent) {
		LOG_DBG("F%d unchanged %d%%", fan, percent);
		return;
	}

	/* Preserve fan startup and RPM conversion behavior from common/fan.c.
	 */
	fan_set_percent_needed(fan, percent);
	LOG_INF("F%d apply %d%%, target %d RPM", fan, percent,
		fan_get_rpm_target(FAN_CH(fan)));
	applied_percent = percent;
}
