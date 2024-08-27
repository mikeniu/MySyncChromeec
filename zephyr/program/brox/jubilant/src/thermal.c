/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "cros_cbi.h"
#include "fan.h"
#include "hooks.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(brox_thermal, LOG_LEVEL_INF);

#define TEMP_SOC TEMP_SENSOR_ID(DT_NODELABEL(temp_soc))

struct fan_step {
	/*
	 * The only sensor temp_soc trigger point
	 */
	int8_t on[TEMP_SENSOR_COUNT];
	/*
	 * The only sensor temp_soc trigger point
	 */
	int8_t off[TEMP_SENSOR_COUNT];
	/* Fan rpm */
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

#define NUM_FAN_LEVELS ARRAY_SIZE(fan_step_table)

static uint8_t thermal_solution;

int fan_table_to_rpm(int fan, int *temp)
{
	/* current fan level */
	static int current_level;
	/* previous fan level */
	static int prev_current_level;
	/* previous sensor temperature */
	static int prev_tmp[TEMP_SENSOR_COUNT];

	int i;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */

	if (temp[TEMP_SOC] < prev_tmp[TEMP_SOC]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_SOC] <= fan_step_table[i].off[TEMP_SOC])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[TEMP_SOC] > prev_tmp[TEMP_SOC]) {
		for (i = current_level; i < NUM_FAN_LEVELS; i++) {
			if (temp[TEMP_SOC] >= fan_step_table[i].on[TEMP_SOC])
				current_level = i;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	if (current_level >= NUM_FAN_LEVELS)
		current_level = NUM_FAN_LEVELS - 1;

	if (current_level != prev_current_level) {
		LOG_INF("temp: %d, prev_temp: %d", temp[TEMP_SOC],
			prev_tmp[TEMP_SOC]);
		LOG_INF("current_level: %d", current_level);
	}

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		prev_tmp[i] = temp[i];
	prev_current_level = current_level;

	return fan_step_table[current_level].rpm[fan];
}

void board_override_fan_control(int fan, int *temp)
{
	/*
	 * In common/fan.c pwm_fan_stop() will turn off fan
	 * when chipset suspend or shutdown.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(fan, fan_table_to_rpm(fan, temp));
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		/* Stop fan when enter S0ix */
		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(fan, 0);
	}
}

test_export_static void thermal_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * Retrieve the fan config.
	 */
	ret = cros_cbi_get_fw_config(FW_THERMAL, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_THERMAL);
		return;
	}

	thermal_solution = val;
}
DECLARE_HOOK(HOOK_INIT, thermal_init, HOOK_PRIO_POST_FIRST);
