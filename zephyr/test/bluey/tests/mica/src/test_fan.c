/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fan.h"
#include "power/qcom.h"
#include "stubs.h"
#include "temp_sensor/temp_sensor.h"

#include <zephyr/ztest.h>

#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_cpu))

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
	RESET_FAKE(chipset_force_shutdown);
	RESET_FAKE(adc_read_channel);
}

ZTEST_SUITE(mica_fan, NULL, NULL, test_fan_before, NULL, NULL);

/* Test fan control duty override for RTC alarm and power states */
ZTEST(mica_fan, test_override_fan_control_duty)
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

/* Test fan table temperature to RPM conversion */
ZTEST(mica_fan, test_fan_table_to_rpm)
{
	int temp[TEMP_SENSOR_COUNT] = { 0 };

	temp[TEMP_CPU] = 30;
	zassert_equal(fan_table_to_rpm(0, temp), 0);

	/*
	 * FAN_LEVEL_DELAY_TIME is 20 in mica fan.c to prevent rapid speed
	 * fluctuations. Verify that 19 consecutive samples stay at level 0
	 * before transitioning to level 1 on the 20th sample (temp >= 47C).
	 */
	temp[TEMP_CPU] = 48;
	for (int i = 0; i < 19; i++) {
		zassert_equal(fan_table_to_rpm(0, temp), 0);
	}
	/* 20th sample triggers transition to level 1 (4500 RPM) */
	zassert_equal(fan_table_to_rpm(0, temp), 4500);

	/* Steady state at level 1 */
	temp[TEMP_CPU] = 44;
	zassert_equal(fan_table_to_rpm(0, temp), 4500);

	/*
	 * Similarly, verify 19 consecutive samples below off threshold
	 * (temp <= 42C) remain at level 1 before dropping to level 0 on 20th.
	 */
	temp[TEMP_CPU] = 40;
	for (int i = 0; i < 19; i++) {
		zassert_equal(fan_table_to_rpm(0, temp), 4500);
	}
	/* 20th sample triggers transition to level 0 (0 RPM) */
	zassert_equal(fan_table_to_rpm(0, temp), 0);
}

/* Test board-level fan control enable and disable override */
ZTEST(mica_fan, test_board_override_fan_control)
{
	int temp[TEMP_SENSOR_COUNT] = { 0 };

	chipset_in_state_fake.return_val = 1;
	adc_read_channel_fake.return_val = 1500;

	board_override_fan_control(0, temp);
	zassert_equal(fan_set_rpm_mode_fake.call_count, 1);
	zassert_equal(fan_set_rpm_target_fake.call_count, 1);

	/* Test sensor read failure -> shutdown on > 10 failures */
	adc_read_channel_fake.return_val = ADC_READ_ERROR;
	for (int i = 0; i < 10; i++) {
		board_override_fan_control(0, temp);
		zassert_equal(chipset_force_shutdown_fake.call_count, 0);
	}
	/* 11th failure */
	board_override_fan_control(0, temp);
	zassert_equal(chipset_force_shutdown_fake.call_count, 1);
	zassert_equal(chipset_force_shutdown_fake.arg0_val,
		      CHIPSET_SHUTDOWN_THERMAL);

	/* Test good sensor read resets fail count */
	RESET_FAKE(chipset_force_shutdown);
	adc_read_channel_fake.return_val = 1500;
	board_override_fan_control(0, temp);

	/* Turn chipset back on and verify less than 10 failures does not
	 * trigger shutdown
	 */
	chipset_in_state_fake.return_val = 1;

	/* Test sensor read failure -> 9 failures */
	adc_read_channel_fake.return_val = ADC_READ_ERROR;
	for (int i = 0; i < 9; i++) {
		board_override_fan_control(0, temp);
		zassert_equal(chipset_force_shutdown_fake.call_count, 0);
	}

	/* Sensor reads valid value on 10th iteration */
	adc_read_channel_fake.return_val = 1500;

	/* Fail count reset, no thermal shutdown */
	board_override_fan_control(0, temp);
	zassert_equal(chipset_force_shutdown_fake.call_count, 0);
}
