/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_chipset.h"
#include "gpio.h"
#include "hooks.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static void test_before(void *fixture)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_usba), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5), 0);
}

ZTEST_SUITE(bluey_board_chipset, NULL, NULL, test_before, NULL, NULL);

/* Test chipset startup direct sequence */
ZTEST(bluey_board_chipset, test_chipset_startup_direct)
{
	board_chipset_startup_bluey();

	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl)), 1,
		"gpio_ec_bl_off_odl should be 1 after startup");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan)),
		1, "gpio_ec_en_pp5000_fan should be 1 after startup");
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_usba)),
		      1, "gpio_ec_en_usba should be 1 after startup");
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr)),
		      1, "gpio_en_hdmi_pwr should be 1 after startup");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3)),
		1, "gpio_ec_en_pp3300_s3 should be 1 after startup");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_oled)), 1,
		"gpio_en_ppvar_oled should be 1 after startup");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5)),
		1, "gpio_ec_en_pp5000_s5 should be 1 after startup");
}

/* Test chipset shutdown direct sequence */
ZTEST(bluey_board_chipset, test_chipset_shutdown_direct)
{
	/* First start up */
	board_chipset_startup_bluey();

	/* Now shutdown */
	board_chipset_shutdown_bluey();

	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl)), 0,
		"gpio_ec_bl_off_odl should be 0 after shutdown");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan)),
		0, "gpio_ec_en_pp5000_fan should be 0 after shutdown");
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_usba)),
		      0, "gpio_ec_en_usba should be 0 after shutdown");
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr)),
		      0, "gpio_en_hdmi_pwr should be 0 after shutdown");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3)),
		0, "gpio_ec_en_pp3300_s3 should be 0 after shutdown");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_oled)), 0,
		"gpio_en_ppvar_oled should be 0 after shutdown");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5)),
		0, "gpio_ec_en_pp5000_s5 should be 0 after shutdown");
}

/* Test chipset startup and shutdown hooks */
ZTEST(bluey_board_chipset, test_chipset_hooks)
{
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl)), 1,
		"gpio_ec_bl_off_odl should be 1 after HOOK_CHIPSET_STARTUP");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan)),
		1,
		"gpio_ec_en_pp5000_fan should be 1 after HOOK_CHIPSET_STARTUP");
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_usba)),
		      1,
		      "gpio_ec_en_usba should be 1 after HOOK_CHIPSET_STARTUP");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr)), 1,
		"gpio_en_hdmi_pwr should be 1 after HOOK_CHIPSET_STARTUP");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3)),
		1,
		"gpio_ec_en_pp3300_s3 should be 1 after HOOK_CHIPSET_STARTUP");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_oled)), 1,
		"gpio_en_ppvar_oled should be 1 after HOOK_CHIPSET_STARTUP");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5)),
		1,
		"gpio_ec_en_pp5000_s5 should be 1 after HOOK_CHIPSET_STARTUP");

	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl)), 0,
		"gpio_ec_bl_off_odl should be 0 after HOOK_CHIPSET_SHUTDOWN");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan)),
		0,
		"gpio_ec_en_pp5000_fan should be 0 after HOOK_CHIPSET_SHUTDOWN");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_usba)), 0,
		"gpio_ec_en_usba should be 0 after HOOK_CHIPSET_SHUTDOWN");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr)), 0,
		"gpio_en_hdmi_pwr should be 0 after HOOK_CHIPSET_SHUTDOWN");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3)),
		0,
		"gpio_ec_en_pp3300_s3 should be 0 after HOOK_CHIPSET_SHUTDOWN");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_oled)), 0,
		"gpio_en_ppvar_oled should be 0 after HOOK_CHIPSET_SHUTDOWN");
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5)),
		0,
		"gpio_ec_en_pp5000_s5 should be 0 after HOOK_CHIPSET_SHUTDOWN");
}
