/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fan.h"
#include "power/qcom.h"
#include "stubs.h"
#include "temp_sensor/temp_sensor.h"

#include <zephyr/ztest.h>

#define TEMP_PMIC TEMP_SENSOR_ID(DT_NODELABEL(temp_pmic))
#define TEMP_SSD TEMP_SENSOR_ID(DT_NODELABEL(temp_ssd))
#define TEMP_CHG TEMP_SENSOR_ID(DT_NODELABEL(temp_charger))

int fan_table_to_rpm(int fan, int *temp);
void board_override_fan_control(int fan, int *temp);

static void test_fan_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(chipset_get_power_on_reason);
	RESET_FAKE(fan_set_duty);
	RESET_FAKE(fan_smart_control);
	RESET_FAKE(fan_set_rpm_mode);
	RESET_FAKE(fan_set_rpm_target);
	RESET_FAKE(chipset_in_state);
}

ZTEST_SUITE(quartz_fan, NULL, NULL, test_fan_before, NULL, NULL);

/* Test fan control duty override for RTC alarm and power states */
ZTEST(quartz_fan, test_override_fan_control_duty)
{
	chipset_get_power_on_reason_fake.return_val = POWER_ON_BY_AC_ON;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_STOPPED);
	zassert_equal(fan_set_duty_fake.call_count, 1);
	zassert_equal(fan_set_duty_fake.arg0_val, 0);
	zassert_equal(fan_set_duty_fake.arg1_val, 0);

	RESET_FAKE(fan_set_duty);
	chipset_get_power_on_reason_fake.return_val = POWER_ON_BY_RTC_ALARM;
	zassert_equal(board_override_fan_control_duty(1), FAN_STATUS_STOPPED);
	zassert_equal(fan_set_duty_fake.call_count, 1);
	zassert_equal(fan_set_duty_fake.arg0_val, 1);

	RESET_FAKE(fan_set_duty);
	chipset_get_power_on_reason_fake.return_val =
		POWER_ON_BY_POWER_BUTTON_PRESSED;
	fan_smart_control_fake.return_val = FAN_STATUS_LOCKED;

	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_LOCKED);
	zassert_equal(fan_smart_control_fake.call_count, 1);
}

/* Test fan RPM hysteresis and ramping up/down */
ZTEST(quartz_fan, test_fan_table_to_rpm_ramp_up_down)
{
	int temp[TEMP_SENSOR_COUNT] = { 0 };

	/*
	 * Fan ramp-up behavior note:
	 * In quartz fan.c, fan_table_to_rpm() checks the ramp-up condition:
	 *   for (i = current_level; i < ARRAY_SIZE(fan_step_table) - 1; i++) {
	 *       if (temp >= fan_step_table[i].on) current_level = i + 1;
	 *   }
	 * Therefore, fan_step_table[i].on defines the threshold to step up from
	 * level i to level i + 1:
	 * - level_0.temp_on (42) triggers step from level 0 -> level 1 (2700
	 * RPM)
	 * - level_1.temp_on (44) triggers step from level 1 -> level 2 (3700
	 * RPM)
	 * - level_2.temp_on (46) triggers step from level 2 -> level 3 (4200
	 * RPM)
	 * - level_3.temp_on (48) triggers step from level 3 -> level 4 (4700
	 * RPM)
	 * - level_4.temp_on (50) triggers step from level 4 -> level 5 (5000
	 * RPM)
	 * - level_5.temp_on (52) triggers step from level 5 -> level 6 (6000
	 * RPM)
	 * - level_6.temp_on (99) is unused for transitions since level 6 is
	 * max.
	 */

	/* Initial cold temperature */
	temp[TEMP_PMIC] = 30;
	temp[TEMP_SSD] = 30;
	temp[TEMP_CHG] = 30;
	zassert_equal(fan_table_to_rpm(0, temp), 0);

	/* Ramp up: PMIC >= 42 triggers level 1 */
	temp[TEMP_PMIC] = 43;
	zassert_equal(fan_table_to_rpm(0, temp), 2700);

	/* PMIC >= 44 triggers level 2 */
	temp[TEMP_PMIC] = 45;
	zassert_equal(fan_table_to_rpm(0, temp), 3700);

	/* PMIC >= 46 triggers level 3 */
	temp[TEMP_PMIC] = 47;
	zassert_equal(fan_table_to_rpm(0, temp), 4200);

	/* PMIC >= 48 triggers level 4 */
	temp[TEMP_PMIC] = 49;
	zassert_equal(fan_table_to_rpm(0, temp), 4700);

	/* PMIC >= 50 triggers level 5 */
	temp[TEMP_PMIC] = 51;
	zassert_equal(fan_table_to_rpm(0, temp), 5000);

	/* PMIC >= 52 triggers level 6 (max) */
	temp[TEMP_PMIC] = 53;
	zassert_equal(fan_table_to_rpm(0, temp), 6000);

	/* Same temperature -> steady state */
	zassert_equal(fan_table_to_rpm(0, temp), 6000);

	/*
	 * Fan ramp-down behavior note:
	 * On ramp-down, fan_table_to_rpm() checks:
	 *   for (i = current_level; i > 0; i--) {
	 *       if (temp < fan_step_table[i].off) current_level = i - 1;
	 *   }
	 * Therefore, fan_step_table[i].off defines the threshold to step down
	 * from level i to level i - 1.
	 */
	temp[TEMP_PMIC] = 48;
	temp[TEMP_SSD] = 20;
	temp[TEMP_CHG] = 20;
	zassert_equal(fan_table_to_rpm(0, temp), 5000);

	temp[TEMP_PMIC] = 46;
	zassert_equal(fan_table_to_rpm(0, temp), 4700);

	temp[TEMP_PMIC] = 44;
	zassert_equal(fan_table_to_rpm(0, temp), 4200);

	temp[TEMP_PMIC] = 42;
	zassert_equal(fan_table_to_rpm(0, temp), 3700);

	temp[TEMP_PMIC] = 40;
	zassert_equal(fan_table_to_rpm(0, temp), 2700);

	temp[TEMP_PMIC] = 35;
	zassert_equal(fan_table_to_rpm(0, temp), 0);
}

/* Test board-level fan control enable and disable override */
ZTEST(quartz_fan, test_board_override_fan_control)
{
	int temp[TEMP_SENSOR_COUNT] = { 0 };

	chipset_in_state_fake.return_val = 1;
	board_override_fan_control(0, temp);
	zassert_equal(fan_set_rpm_mode_fake.call_count, 1);
	zassert_equal(fan_set_rpm_target_fake.call_count, 1);

	/* Chipset not ON */
	RESET_FAKE(fan_set_rpm_mode);
	RESET_FAKE(fan_set_rpm_target);
	chipset_in_state_fake.return_val = 0;
	board_override_fan_control(0, temp);
	zassert_equal(fan_set_rpm_mode_fake.call_count, 0);
	zassert_equal(fan_set_rpm_target_fake.call_count, 0);
}
