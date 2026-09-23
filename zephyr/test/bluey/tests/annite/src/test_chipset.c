/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_chipset.h"
#include "gpio.h"
#include "hooks.h"
#include "power/qcom.h"
#include "stubs.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

static void test_chipset_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Reset power rails to 0 (all these specific lines are active-high) */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_sys_throttle_mira), 0);
}

ZTEST_SUITE(annite_chipset, NULL, NULL, test_chipset_before, NULL, NULL);

/* Test chipset startup and shutdown sequence */
ZTEST(annite_chipset, test_startup_and_shutdown)
{
	board_chipset_startup_annite();
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

	board_chipset_shutdown_annite();
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

/* Test pre-init and hard off power rail handling */
ZTEST(annite_chipset, test_pre_init_and_hard_off)
{
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000)), 1);

	hook_notify(HOOK_CHIPSET_HARD_OFF);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000)), 1);

	/* Advance virtual time to allow deferred disable_pp5000 work to run */
	k_sleep(K_MSEC(5200));
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000)), 0);
}

/* Test chipset suspend and resume transitions */
ZTEST(annite_chipset, test_suspend_resume)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 1);
	board_chipset_suspend_annite();
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled)),
		0);

	board_chipset_resume_annite();
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled)),
		1);
}
