/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "hooks.h"
#include "power/qcom.h"
#include "stubs.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

extern const struct deferred_data disable_pp5000_data;

void board_chipset_startup_pic(void);
void board_chipset_shutdown_pic(void);
void board_chipset_suspend_pic(void);
void board_chipset_resume_pic(void);

static void test_chipset_before(void *fixture)
{
	ARG_UNUSED(fixture);

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_sys_throttle_mira), 0);
}

ZTEST_SUITE(pic_chipset, NULL, NULL, test_chipset_before, NULL, NULL);

ZTEST(pic_chipset, test_startup_and_shutdown)
{
	board_chipset_startup_pic();
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

	board_chipset_shutdown_pic();
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

ZTEST(pic_chipset, test_hard_off)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 1);
	hook_notify(HOOK_CHIPSET_HARD_OFF);

	/* Trigger deferred disable_pp5000 routine */
	disable_pp5000_data.work->work.handler(&disable_pp5000_data.work->work);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000)), 0);
}

ZTEST(pic_chipset, test_suspend_resume)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 1);
	board_chipset_suspend_pic();
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled)),
		0);

	board_chipset_resume_pic();
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled)),
		1);
}
