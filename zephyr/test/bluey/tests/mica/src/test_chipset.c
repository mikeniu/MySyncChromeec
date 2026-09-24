/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_chipset.h"
#include "gpio.h"
#include "hooks.h"
#include "lb_policy.h"
#include "power/qcom.h"
#include "stubs.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static void test_chipset_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(chipset_get_power_on_reason);
	RESET_FAKE(lb_set_diag_policy);

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_sys_throttle_mira), 0);
}

ZTEST_SUITE(mica_chipset, NULL, NULL, test_chipset_before, NULL, NULL);

/* Test chipset startup and shutdown sequence */
ZTEST(mica_chipset, test_startup_and_shutdown)
{
	board_chipset_startup_mica();
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl)), 1);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan)),
		1);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en)),
		      1);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled)),
		1);
	zassert_equal(gpio_pin_get_dt(
			      GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl)),
		      1);

	board_chipset_shutdown_mica();
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl)), 0);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan)),
		0);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en)),
		      0);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled)),
		0);
	zassert_equal(gpio_pin_get_dt(
			      GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl)),
		      0);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
			      gpio_ec_sys_throttle_mira)),
		      0);
}

/* Test chipset pre-init and hard off power fail handling */
ZTEST(mica_chipset, test_pre_init_and_hard_off_power_fail)
{
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000)), 1);

	chipset_get_power_on_reason_fake.return_val =
		POWER_ON_BY_POWER_BUTTON_PRESSED;
	hook_notify(HOOK_CHIPSET_HARD_OFF);

	zassert_equal(lb_set_diag_policy_fake.call_count, 1);
	zassert_equal(lb_set_diag_policy_fake.arg0_val,
		      LED_ALT_POLICY_DIAG_PWR);
	zassert_equal(lb_set_diag_policy_fake.arg1_val, 600000);
}

/* Test hard off behavior on AC insertion */
ZTEST(mica_chipset, test_hard_off_ac_on)
{
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	chipset_get_power_on_reason_fake.return_val = POWER_ON_BY_AC_ON;
	hook_notify(HOOK_CHIPSET_HARD_OFF);

	zassert_equal(lb_set_diag_policy_fake.call_count, 0);
}

/* Test chipset suspend and resume transitions */
ZTEST(mica_chipset, test_suspend_resume)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 1);
	board_chipset_suspend_mica();
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled)),
		0);

	board_chipset_resume_mica();
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled)),
		1);
}
