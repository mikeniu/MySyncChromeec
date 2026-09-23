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

static uint32_t fake_board_id;

static int fake_cbi_get_board_version(uint32_t *version)
{
	*version = fake_board_id;
	return EC_SUCCESS;
}

static void test_throttle_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(cbi_get_board_version);
	RESET_FAKE(battery_is_present);

	fake_board_id = 0;
	cbi_get_board_version_fake.custom_fake = fake_cbi_get_board_version;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_sys_throttle_mira), 0);
}

ZTEST_SUITE(mica_throttle, NULL, NULL, test_throttle_before, NULL, NULL);

/* Test throttle initialization on HOOK_INIT */
ZTEST(mica_throttle, test_throttle_init)
{
	/* Early board (<= 1): throttle_init sets GPIO flags */
	fake_board_id = 1;
	hook_notify(HOOK_INIT);
	zassert_equal(cbi_get_board_version_fake.call_count, 1);

	/* Late board (> 1): throttle_init skips setting GPIO flags */
	fake_board_id = 2;
	hook_notify(HOOK_INIT);
	zassert_equal(cbi_get_board_version_fake.call_count, 2);

	/* CBI error: falls back to board_id 0 (<= 1) */
	cbi_get_board_version_fake.custom_fake = NULL;
	cbi_get_board_version_fake.return_val = EC_ERROR_UNKNOWN;
	hook_notify(HOOK_INIT);
	zassert_equal(cbi_get_board_version_fake.call_count, 3);
}

/* Test CPU throttling controls when chipset is ON */
ZTEST(mica_throttle, test_chipset_throttle_cpu)
{
	/* Chipset not ON: throttling should return early and not change pin */
	chipset_in_state_fake.return_val = 0;
	chipset_throttle_cpu(1);
	zassert_equal(chipset_in_state_fake.call_count, 1);
	zassert_equal(chipset_in_state_fake.arg0_val, CHIPSET_STATE_ON);
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
ZTEST(mica_throttle, test_ac_only_boot)
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
