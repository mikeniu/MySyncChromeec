/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fan.h"
#include "power/qcom.h"
#include "stubs.h"

#include <zephyr/ztest.h>

static void test_before(void *fixture)
{
	RESET_FAKE(chipset_get_power_on_reason);
	RESET_FAKE(fan_set_duty);
	RESET_FAKE(fan_get_rpm_actual);
	RESET_FAKE(fan_set_rpm_mode);
	RESET_FAKE(fan_set_rpm_target);
}

ZTEST_SUITE(quenbi_board_fan, NULL, NULL, test_before, NULL, NULL);

/* Test fan duty override on AC power insertion */
ZTEST(quenbi_board_fan, test_override_fan_control_duty_ac_on)
{
	enum fan_status status;

	chipset_get_power_on_reason_fake.return_val = POWER_ON_BY_AC_ON;

	status = board_override_fan_control_duty(0);

	zassert_equal(status, FAN_STATUS_STOPPED,
		      "Fan status should be STOPPED when power on by AC");
	zassert_equal(fan_set_duty_fake.call_count, 1,
		      "fan_set_duty should be called once");
	zassert_equal(fan_set_duty_fake.arg0_val, 0, "fan channel should be 0");
	zassert_equal(fan_set_duty_fake.arg1_val, 0, "duty should be set to 0");
}

/* Test fan duty override on RTC alarm power on */
ZTEST(quenbi_board_fan, test_override_fan_control_duty_rtc_alarm)
{
	enum fan_status status;

	chipset_get_power_on_reason_fake.return_val = POWER_ON_BY_RTC_ALARM;

	status = board_override_fan_control_duty(0);

	zassert_equal(
		status, FAN_STATUS_STOPPED,
		"Fan status should be STOPPED when power on by RTC alarm");
	zassert_equal(fan_set_duty_fake.call_count, 1,
		      "fan_set_duty should be called once");
	zassert_equal(fan_set_duty_fake.arg0_val, 0, "fan channel should be 0");
	zassert_equal(fan_set_duty_fake.arg1_val, 0, "duty should be set to 0");
}

/* Test fan duty override when actual RPM is zero */
ZTEST(quenbi_board_fan, test_override_fan_control_duty_rpm_zero)
{
	enum fan_status status;

	chipset_get_power_on_reason_fake.return_val =
		POWER_ON_BY_POWER_BUTTON_PRESSED;
	fan_get_rpm_actual_fake.return_val = 0;

	status = board_override_fan_control_duty(0);

	zassert_equal(status, FAN_STATUS_LOCKED,
		      "Fan status should be LOCKED when waiting for valid RPM");
	zassert_equal(fan_set_duty_fake.call_count, 1,
		      "fan_set_duty should be called once with 50% duty");
	zassert_equal(fan_set_duty_fake.arg0_val, 0, "fan channel should be 0");
	zassert_equal(fan_set_duty_fake.arg1_val, 50,
		      "duty should be set to 50%");
	zassert_equal(fan_set_rpm_mode_fake.call_count, 0,
		      "fan_set_rpm_mode should not be called");
}

/* Test fan duty override calculation for valid actual RPM */
ZTEST(quenbi_board_fan, test_override_fan_control_duty_valid_rpm)
{
	enum fan_status status;

	chipset_get_power_on_reason_fake.return_val =
		POWER_ON_BY_POWER_BUTTON_PRESSED;
	fan_get_rpm_actual_fake.return_val = 3000;

	status = board_override_fan_control_duty(0);

	zassert_equal(status, FAN_STATUS_LOCKED,
		      "Fan status should be LOCKED when valid RPM detected");
	zassert_equal(fan_set_rpm_mode_fake.call_count, 1,
		      "fan_set_rpm_mode should be called once");
	zassert_equal(fan_set_rpm_mode_fake.arg0_val, 0,
		      "fan channel should be 0");
	zassert_equal(fan_set_rpm_mode_fake.arg1_val, 1,
		      "rpm_mode should be true");
	zassert_equal(fan_set_rpm_target_fake.call_count, 1,
		      "fan_set_rpm_target should be called once");
	zassert_equal(fan_set_rpm_target_fake.arg0_val, 0,
		      "fan channel should be 0");
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 6000,
		      "rpm_target should be 6000");
}
