/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fan.h"
#include "power/qcom.h"
#include "stubs.h"
#include "temp_sensor/temp_sensor.h"
#include "timer.h"

#include <zephyr/sys/time_units.h>
#include <zephyr/ztest.h>

#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_cpu))

/* Highest RPM in the board fan step table (level 7). */
#define FAN_MAX_RPM 8300

int fan_table_to_rpm(int fan, int *temp);
void board_override_fan_control(int fan, int *temp);

static timestamp_t fake_time;

static void test_fan_before(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(chipset_get_power_on_reason);
	RESET_FAKE(fan_set_duty);
	RESET_FAKE(fan_get_duty);
	RESET_FAKE(fan_set_rpm_mode);
	RESET_FAKE(fan_set_rpm_target);
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(chipset_force_shutdown);
	RESET_FAKE(adc_read_channel);

	fake_time.val = 0;
	get_time_mock = &fake_time;

	fan_data[0].rpm_actual = 0;
	fan_data[0].rpm_target = 0;
}

static void test_fan_after(void *fixture)
{
	ARG_UNUSED(fixture);
	get_time_mock = NULL;
}

ZTEST_SUITE(pic_fan, NULL, NULL, test_fan_before, test_fan_after, NULL);

/* Test fan control duty override for RTC alarm and power states */
ZTEST(pic_fan, test_override_fan_control_duty)
{
	int calls;

	/* Start past any one-second gate left behind by earlier runs. */
	fake_time.val = 10 * USEC_PER_SEC;

	/* AC wake: the AP stays in the charging loop, so the fan is off. */
	chipset_get_power_on_reason_fake.return_val = POWER_ON_BY_AC_ON;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_STOPPED);
	zassert_equal(fan_set_duty_fake.call_count, 1);
	zassert_equal(fan_set_duty_fake.arg0_val, 0);
	zassert_equal(fan_set_duty_fake.arg1_val, 0);

	RESET_FAKE(fan_set_duty);
	chipset_get_power_on_reason_fake.return_val = POWER_ON_BY_RTC_ALARM;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_STOPPED);
	zassert_equal(fan_set_duty_fake.call_count, 1);
	zassert_equal(fan_set_duty_fake.arg1_val, 0);

	/* Normal wake: an idle fan (duty 0, target 0) stays stopped. */
	RESET_FAKE(fan_set_duty);
	chipset_get_power_on_reason_fake.return_val =
		POWER_ON_BY_POWER_BUTTON_PRESSED;
	fan_get_duty_fake.return_val = 0;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_STOPPED);
	zassert_equal(fan_set_duty_fake.call_count, 0);

	/* The gate blocks a second adjustment within the same second. */
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_STOPPED);
	zassert_equal(fan_set_duty_fake.call_count, 0);

	/* Ramp up 1%/s while the actual RPM is below the target. */
	fan_data[0].rpm_actual = 0;
	fan_data[0].rpm_target = 4500;
	fake_time.val += USEC_PER_SEC;
	fan_get_duty_fake.return_val = 0;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_CHANGING);
	zassert_equal(fan_set_duty_fake.arg1_val, 1);

	fake_time.val += USEC_PER_SEC;
	fan_get_duty_fake.return_val = 1;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_CHANGING);
	zassert_equal(fan_set_duty_fake.arg1_val, 2);

	/* No more than one adjustment per second. */
	calls = fan_set_duty_fake.call_count;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_CHANGING);
	zassert_equal(fan_set_duty_fake.call_count, calls);

	/* Full duty with the fan still slow is reported as frustrated. */
	fake_time.val += USEC_PER_SEC;
	fan_get_duty_fake.return_val = 100;
	calls = fan_set_duty_fake.call_count;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_FRUSTRATED);
	zassert_equal(fan_set_duty_fake.call_count, calls);

	/* In-band RPM holds the duty and reports locked. */
	fake_time.val += USEC_PER_SEC;
	fan_data[0].rpm_actual = 4500;
	fan_get_duty_fake.return_val = 50;
	calls = fan_set_duty_fake.call_count;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_LOCKED);
	zassert_equal(fan_set_duty_fake.call_count, calls);

	/* Slow down 2%/s when the target is 0. */
	fake_time.val += USEC_PER_SEC;
	fan_data[0].rpm_target = 0;
	fan_data[0].rpm_actual = 4500;
	fan_get_duty_fake.return_val = 10;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_CHANGING);
	zassert_equal(fan_set_duty_fake.arg1_val, 8);

	fake_time.val += USEC_PER_SEC;
	fan_get_duty_fake.return_val = 3; /* Clamped to 1. */
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_CHANGING);
	zassert_equal(fan_set_duty_fake.arg1_val, 1);

	fake_time.val += USEC_PER_SEC;
	fan_get_duty_fake.return_val = 2; /* Clamped to 0, so stopped. */
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_STOPPED);
	zassert_equal(fan_set_duty_fake.arg1_val, 0);

	/* A gated call with a zero target still reports changing. */
	calls = fan_set_duty_fake.call_count;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_CHANGING);
	zassert_equal(fan_set_duty_fake.call_count, calls);

	/* A zero target with zero duty is stopped. */
	fake_time.val += USEC_PER_SEC;
	fan_get_duty_fake.return_val = 0;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_STOPPED);

	/* Running above the target at minimum duty is frustrated. */
	fake_time.val += USEC_PER_SEC;
	fan_data[0].rpm_target = 4500;
	fan_data[0].rpm_actual = 5000;
	fan_get_duty_fake.return_val = 1;
	calls = fan_set_duty_fake.call_count;
	zassert_equal(board_override_fan_control_duty(0), FAN_STATUS_FRUSTRATED);
	zassert_equal(fan_set_duty_fake.call_count, calls);
}

/* Test fan table temperature to RPM conversion */
ZTEST(pic_fan, test_fan_table_to_rpm)
{
	int temp[TEMP_SENSOR_COUNT] = { 0 };

	/* Below the idle-off threshold the fan is always off. */
	temp[TEMP_CPU] = 30;
	zassert_equal(fan_table_to_rpm(0, temp), 0);

	/* 46C immediately steps level 0 -> 1. */
	temp[TEMP_CPU] = 46;
	zassert_equal(fan_table_to_rpm(0, temp), 4500);

	/* 45C stays in level 1 (hysteresis band). */
	temp[TEMP_CPU] = 45;
	zassert_equal(fan_table_to_rpm(0, temp), 4500);

	/* 40C steps level 1 -> 0, though not below the idle threshold. */
	temp[TEMP_CPU] = 40;
	zassert_equal(fan_table_to_rpm(0, temp), 0);

	/* 49C steps up to level 2. */
	temp[TEMP_CPU] = 49;
	zassert_equal(fan_table_to_rpm(0, temp), 5200);

	/* 71C steps up to level 6. */
	temp[TEMP_CPU] = 71;
	zassert_equal(fan_table_to_rpm(0, temp), 8000);

	/* 72C steps up to the highest level. */
	temp[TEMP_CPU] = 72;
	zassert_equal(fan_table_to_rpm(0, temp), 8300);

	/* 68C steps back down to level 6. */
	temp[TEMP_CPU] = 68;
	zassert_equal(fan_table_to_rpm(0, temp), 8000);

	/* Below the idle-off threshold forces level 0 from any level. */
	temp[TEMP_CPU] = 39;
	zassert_equal(fan_table_to_rpm(0, temp), 0);
}

/* Test board-level fan control enable and disable override */
ZTEST(pic_fan, test_board_override_fan_control)
{
	int temp[TEMP_SENSOR_COUNT] = { 0 };

	/* Chipset off: nothing is programmed by the thermal loop. */
	chipset_in_state_fake.return_val = 0;
	board_override_fan_control(0, temp);
	zassert_equal(fan_set_rpm_mode_fake.call_count, 0);
	zassert_equal(fan_set_rpm_target_fake.call_count, 0);

	/* Chipset on with a valid reading programs an RPM target. */
	chipset_in_state_fake.return_val = 1;
	adc_read_channel_fake.return_val = 1500;
	board_override_fan_control(0, temp);
	zassert_equal(fan_set_rpm_mode_fake.call_count, 1);
	zassert_equal(fan_set_rpm_mode_fake.arg1_val, 1);
	zassert_equal(fan_set_rpm_target_fake.call_count, 1);

	/* The first 9 missing samples only count up. */
	adc_read_channel_fake.return_val = ADC_READ_ERROR;
	for (int i = 0; i < 9; i++)
		board_override_fan_control(0, temp);
	zassert_equal(chipset_force_shutdown_fake.call_count, 0);

	/* 10th missing sample raises the fan to max RPM. */
	board_override_fan_control(0, temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, FAN_MAX_RPM);
	zassert_equal(chipset_force_shutdown_fake.call_count, 0);

	/* Samples 11 to 19 keep the fan at max RPM without a shutdown. */
	for (int i = 0; i < 9; i++)
		board_override_fan_control(0, temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, FAN_MAX_RPM);
	zassert_equal(chipset_force_shutdown_fake.call_count, 0);

	/* 20th missing sample forces a thermal shutdown. */
	board_override_fan_control(0, temp);
	zassert_equal(chipset_force_shutdown_fake.call_count, 1);
	zassert_equal(chipset_force_shutdown_fake.arg0_val,
		      CHIPSET_SHUTDOWN_THERMAL);

	/* Chipset off clears the failure count and the averaged temperature. */
	RESET_FAKE(chipset_force_shutdown);
	chipset_in_state_fake.return_val = 0;
	board_override_fan_control(0, temp);
	zassert_equal(chipset_force_shutdown_fake.call_count, 0);

	/* A failure right after power-on has no average, so the target is 0. */
	chipset_in_state_fake.return_val = 1;
	adc_read_channel_fake.return_val = ADC_READ_ERROR;
	board_override_fan_control(0, temp);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 0);
	zassert_equal(chipset_force_shutdown_fake.call_count, 0);

	/* A single failure after a valid sample does not shut down. */
	adc_read_channel_fake.return_val = 1500;
	board_override_fan_control(0, temp);
	adc_read_channel_fake.return_val = ADC_READ_ERROR;
	board_override_fan_control(0, temp);
	zassert_equal(chipset_force_shutdown_fake.call_count, 0);
}
