/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "hooks.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

void board_chipset_startup_quenbi(void);
void board_chipset_shutdown_quenbi(void);

static void test_before(void *fixture)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usb_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl), 0);
}

ZTEST_SUITE(quenbi_board_chipset, NULL, NULL, test_before, NULL, NULL);

ZTEST(quenbi_board_chipset, test_chipset_startup)
{
	board_chipset_startup_quenbi();

	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usb_en)),
		      1, "gpio_ec_usb_en should be 1 after startup");
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en)),
		      1, "gpio_ec_3v_s3_en should be 1 after startup");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled)),
		1, "gpio_ec_en_ppvar_oled should be 1 after startup");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000)), 1,
		"gpio_ec_en_pp5000 should be 1 after startup");
	zassert_equal(gpio_pin_get_dt(
			      GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl)),
		      1, "gpio_ec_batt_i2c_en_odl should be 1 after startup");

	/* Allow deferred HDMI enable to run */
	k_msleep(20);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr)),
		      1,
		      "gpio_en_hdmi_pwr should be 1 after deferred execution");
}

ZTEST(quenbi_board_chipset, test_chipset_shutdown)
{
	/* Startup first */
	board_chipset_startup_quenbi();
	k_msleep(20);

	/* Shutdown */
	board_chipset_shutdown_quenbi();

	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usb_en)),
		      0, "gpio_ec_usb_en should be 0 after shutdown");
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr)),
		      0, "gpio_en_hdmi_pwr should be 0 after shutdown");
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en)),
		      0, "gpio_ec_3v_s3_en should be 0 after shutdown");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled)),
		0, "gpio_ec_en_ppvar_oled should be 0 after shutdown");
	zassert_equal(gpio_pin_get_dt(
			      GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl)),
		      0, "gpio_ec_batt_i2c_en_odl should be 0 after shutdown");

	/* Allow deferred PP5000 disable to run */
	k_msleep(20);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000)), 0,
		"gpio_ec_en_pp5000 should be 0 after deferred execution");
}

ZTEST(quenbi_board_chipset, test_chipset_hooks)
{
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usb_en)),
		      1, "HOOK_CHIPSET_STARTUP should enable pins");

	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usb_en)),
		      0, "HOOK_CHIPSET_SHUTDOWN should disable pins");
}
