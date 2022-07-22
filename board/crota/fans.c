/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Physical fans. These are logically separate from pwm_channels. */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "fan_chip.h"
#include "fan.h"
#include "hooks.h"
#include "pwm.h"
#include "thermal.h"
#include "util.h"

#define SENSOR_SOC_FAN_OFF 40
#define SENSOR_SOC_FAN_MID 51
#define SENSOR_SOC_FAN_MAX 53

/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

static const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

static const struct fan_rpm fan_rpm_cpu_0 = {
	.rpm_min = 2200,
	.rpm_start = 2200,
	.rpm_max = 3700,
};

static const struct fan_rpm fan_rpm_cpu_1 = {
	.rpm_min = 3700,
	.rpm_start = 3700,
	.rpm_max = 4000,
};

static const struct fan_rpm fan_rpm_charger = {
	.rpm_min = 4000,
	.rpm_start = 4000,
	.rpm_max = 4700,
};

struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_cpu_0,
	},
};

static void fan_set_percent(int fan, int pct, int soc_temp_c, bool fan_triggered)
{
	int new_rpm;

	if (fan_triggered)
		fans[fan].rpm = &fan_rpm_charger;
	else if (soc_temp_c > SENSOR_SOC_FAN_MID)
		fans[fan].rpm = &fan_rpm_cpu_1;
	else
		fans[fan].rpm = &fan_rpm_cpu_0;

	new_rpm = fan_percent_to_rpm(fan, pct);
	fan_set_rpm_target(FAN_CH(fan), new_rpm);
}

void board_override_fan_control(int fan, int *tmp)
{
	/*
	 * Crota's fan speed is control by three sensors.
	 *
	 * Sensor SOC control high loading's speed.
	 * Sensor charger control the speed when system's temperature
	 * is too high.
	 *
	 * When sensor charger is not triggered, the fan is control
	 * by sensor SOC. Sensor SOC has two slopes for fan speed.
	 *
	 * When sensor charger is triggered, the fan speed is only
	 * control by sensor charger, avoid heat damage to system.
	 */
	int soc_temp_c;
	int charger_temp_c;
	int sensor_soc_fan_off;
	int sensor_soc_fan_mid;
	int sensor_soc_fan_max;
	int pct;
	int sensor_soc;
	int sensor_charger;
	bool fan_triggered;

	soc_temp_c = tmp[TEMP_SENSOR_1_SOC];
	charger_temp_c = tmp[TEMP_SENSOR_3_CHARGER];
	sensor_soc_fan_off = C_TO_K(SENSOR_SOC_FAN_OFF);
	sensor_soc_fan_mid = C_TO_K(SENSOR_SOC_FAN_MID);
	sensor_soc_fan_max = C_TO_K(SENSOR_SOC_FAN_MAX);

	if (soc_temp_c > SENSOR_SOC_FAN_MID){
		thermal_params[TEMP_SENSOR_1_SOC].temp_fan_off = sensor_soc_fan_mid;
		thermal_params[TEMP_SENSOR_1_SOC].temp_fan_max = sensor_soc_fan_max;
	} else {
		thermal_params[TEMP_SENSOR_1_SOC].temp_fan_off = sensor_soc_fan_off;
		thermal_params[TEMP_SENSOR_1_SOC].temp_fan_max = sensor_soc_fan_mid;
	}

	sensor_soc = thermal_fan_percent(
					 thermal_params[TEMP_SENSOR_1_SOC].temp_fan_off,
					 thermal_params[TEMP_SENSOR_1_SOC].temp_fan_max,
					 C_TO_K(soc_temp_c));
	sensor_charger = thermal_fan_percent(
						 thermal_params[TEMP_SENSOR_3_CHARGER].temp_fan_off,
					     thermal_params[TEMP_SENSOR_3_CHARGER].temp_fan_max,
					     C_TO_K(charger_temp_c));

	if (sensor_charger) {
		fan_triggered = true;
		pct = sensor_charger;
	} else {
		fan_triggered = false;
		pct = sensor_soc;
	}
	/* transfer percent to rpm */
	fan_set_percent(fan, pct, soc_temp_c, fan_triggered);
}
