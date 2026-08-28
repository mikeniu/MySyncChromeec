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
	RESET_FAKE(fan_smart_control);
}

ZTEST_SUITE(bluey_board_fan, NULL, NULL, test_before, NULL, NULL);

/* Test fan duty override on AC power insertion */
ZTEST(bluey_board_fan, test_override_fan_control_duty_ac_on)
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
	zassert_equal(fan_smart_control_fake.call_count, 0,
		      "fan_smart_control should not be called");
}

/* Test fan duty override during normal power on */
ZTEST(bluey_board_fan, test_override_fan_control_duty_normal_power_on)
{
	enum fan_status status;

	chipset_get_power_on_reason_fake.return_val =
		POWER_ON_BY_POWER_BUTTON_PRESSED;
	fan_smart_control_fake.return_val = FAN_STATUS_LOCKED;

	status = board_override_fan_control_duty(1);

	zassert_equal(status, FAN_STATUS_LOCKED,
		      "Fan status should be from fan_smart_control");
	zassert_equal(fan_set_duty_fake.call_count, 0,
		      "fan_set_duty should not be called");
	zassert_equal(fan_smart_control_fake.call_count, 1,
		      "fan_smart_control should be called once");
	zassert_equal(fan_smart_control_fake.arg0_val, 1,
		      "fan channel should be 1");
}
