/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mica Fan configuration */

#include "chipset.h"
#include "common.h"
#include "fan.h"
#include "hooks.h"
#include "power/qcom.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mica_fan, LOG_LEVEL_INF);

#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_cpu))

static int8_t current_level;
static int8_t prev_current_level;
static int8_t up_level_delay_count;
static int8_t down_level_delay_count;
static int8_t sensor_detect_fail_count;

struct fan_step {
	int8_t on[1];
	int8_t off[1];
	/* Fan rpm */
	uint16_t rpm[FAN_CH_COUNT];
};

#define FAN_LEVEL_DELAY_TIME 20

#define FAN_TABLE_ENTRY(nd)                     \
	{                                       \
		.on = DT_PROP(nd, temp_on),     \
		.off = DT_PROP(nd, temp_off),   \
		.rpm = DT_PROP(nd, rpm_target), \
	},

static const struct fan_step fan_step_table[] = { DT_FOREACH_CHILD(
	DT_NODELABEL(fan_steps), FAN_TABLE_ENTRY) };

int fan_table_to_rpm(int fan, int *temp)
{
	/*
	 * Add the 10s delay mechanism to avoid the fans from frequently
	 * changing to fast.
	 */
	if (current_level < ARRAY_SIZE(fan_step_table) - 1 &&
	    temp[TEMP_CPU] >= fan_step_table[current_level].on[TEMP_CPU]) {
		up_level_delay_count++;
		if (up_level_delay_count >= FAN_LEVEL_DELAY_TIME) {
			current_level++;
			up_level_delay_count = 0;
		}
		down_level_delay_count = 0;
	} else if (current_level > 0 &&
		   temp[TEMP_CPU] <=
			   fan_step_table[current_level].off[TEMP_CPU]) {
		down_level_delay_count++;
		if (down_level_delay_count >= FAN_LEVEL_DELAY_TIME) {
			current_level--;
			down_level_delay_count = 0;
		}
		up_level_delay_count = 0;
	} else {
		up_level_delay_count = 0;
		down_level_delay_count = 0;
	}

	if (current_level != prev_current_level)
		LOG_INF("Fan current_level: %d", current_level);

	prev_current_level = current_level;

	return fan_step_table[current_level].rpm[fan];
}

/**
 * Override default fan control.
 *
 * On Mica, the TACH signal's pull-up rail (+3V_VREG_MISC)
 * takes time to stabilize after AP power-on. During this
 * time, the TACH reports 0 RPM.
 */
enum fan_status board_override_fan_control_duty(int ch)
{
	enum power_on_event_t power_on_reason;

	power_on_reason = chipset_get_power_on_reason();

	/* If the last power-on is due to AC or RTC, which enters the charging
	 * loop in the AP firmware stop the fan */
	if (POWER_ON_BY_AC_ON == power_on_reason ||
	    POWER_ON_BY_RTC_ALARM == power_on_reason) {
		fan_set_duty(ch, 0);
		return FAN_STATUS_STOPPED;
	}

	return fan_smart_control(ch);
}

void board_override_fan_control(int fan, int *temp)
{
	int t, rv;
	/*
	 * In common/fan.c pwm_fan_stop() will turn off fan
	 * when chipset suspend or shutdown.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/*
		 * Mica adds a sensor detect fail protection mechanism.
		 * If the sensor fails to read for 10 consecutive times,
		 * it will force shutdown the AP to prevent overheating.
		 */
		rv = temp_sensor_read(TEMP_CPU, &t);
		if (rv != EC_SUCCESS) {
			sensor_detect_fail_count++;
			if (sensor_detect_fail_count > 10) {
				chipset_force_shutdown(
					CHIPSET_SHUTDOWN_THERMAL);
				LOG_INF("Sensor detects fail for 10s, turn to force shutdown");
			}
		}

		fan_set_rpm_mode(fan, true);
		fan_set_rpm_target(fan, fan_table_to_rpm(fan, temp));
	} else {
		sensor_detect_fail_count = 0;
	}
}
