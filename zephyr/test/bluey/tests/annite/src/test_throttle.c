/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "chipset.h"
#include "gpio.h"
#include "hooks.h"
#include "stubs.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static void test_throttle_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(battery_is_present);

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_sys_throttle_mira), 0);
}

ZTEST_SUITE(annite_throttle, NULL, NULL, test_throttle_before, NULL, NULL);

/* Test CPU throttling controls when chipset is ON */
ZTEST(annite_throttle, test_chipset_throttle_cpu)
{
	/* Chipset not ON */
	chipset_in_state_fake.return_val = 0;
	chipset_throttle_cpu(1);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
			      gpio_ec_sys_throttle_mira)),
		      0);

	/* Chipset ON and battery present */
	chipset_in_state_fake.return_val = 1;
	battery_is_present_fake.return_val = BP_YES;
	hook_notify(HOOK_CHIPSET_PRE_INIT);

	chipset_throttle_cpu(1);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
			      gpio_ec_sys_throttle_mira)),
		      1);

	chipset_throttle_cpu(0);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
			      gpio_ec_sys_throttle_mira)),
		      0);
}

/* Test PROCHOT throttling during AC-only boot without battery */
ZTEST(annite_throttle, test_ac_only_boot)
{
	/* Boot without battery */
	battery_is_present_fake.return_val = BP_NO;
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
			      gpio_ec_sys_throttle_mira)),
		      1);

	/* Throttling command should be ignored during AC-only boot */
	chipset_in_state_fake.return_val = 1;
	chipset_throttle_cpu(0);
	zassert_equal(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
			      gpio_ec_sys_throttle_mira)),
		      1);
}
