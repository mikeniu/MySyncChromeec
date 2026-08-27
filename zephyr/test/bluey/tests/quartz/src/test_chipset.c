/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "gpio.h"
#include "hooks.h"
#include "stubs.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

void s3_power_interrupt(enum gpio_signal signal);

static void test_chipset_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(battery_is_present);

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pp1800_l1i_s3_ec), 0);
}

ZTEST_SUITE(quartz_chipset, NULL, NULL, test_chipset_before, NULL, NULL);

ZTEST(quartz_chipset, test_startup_and_shutdown)
{
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec)), 1);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en)), 1);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl)),
		      1);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan)),
		1);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled)),
		1);

	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec)), 0);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en)), 0);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl)),
		      0);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan)),
		0);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled)),
		0);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira)),
		0);
}

ZTEST(quartz_chipset, test_s3_power_interrupt)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pp1800_l1i_s3_ec), 1);
	s3_power_interrupt(GPIO_UNIMPLEMENTED);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3)),
		1);

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pp1800_l1i_s3_ec), 0);
	s3_power_interrupt(GPIO_UNIMPLEMENTED);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3)),
		0);
}

ZTEST(quartz_chipset, test_enable_s3_interrupt_hook)
{
	hook_notify(HOOK_INIT);
}

ZTEST(quartz_chipset, test_pre_init_and_hard_off)
{
	/* Battery present */
	battery_is_present_fake.return_val = BP_YES;
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5)),
		1);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira)),
		0);

	/* Battery not present -> prochot throttle asserted */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira), 0);
	battery_is_present_fake.return_val = BP_NO;
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	zassert_equal(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira)),
		1);

	/* Hard off triggers deferred disable */
	hook_notify(HOOK_CHIPSET_HARD_OFF);
}
