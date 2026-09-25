/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "chipset.h"
#include "hooks.h"
#include "stubs.h"

#include <zephyr/ztest.h>

void poll_battery_info(void);
void board_chipset_hard_off(void);
void board_chipset_pre_init(void);
enum battery_access_type battery_check_access_limit(void);
void board_battery_compensate_params(struct batt_params *batt);

static int soc_change_hook_count;
static void test_soc_change_hook(void)
{
	soc_change_hook_count++;
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, test_soc_change_hook, HOOK_PRIO_DEFAULT);

static void test_before(void *fixture)
{
	RESET_FAKE(battery_is_present);
	RESET_FAKE(battery_poll_dynamic_info);
	RESET_FAKE(update_static_battery_info);
	RESET_FAKE(chipset_in_state);
	soc_change_hook_count = 0;
}

ZTEST_SUITE(bluey_baseboard_battery, NULL, NULL, test_before, NULL, NULL);

/* Test battery polling does not run when chipset is not hard off */
ZTEST(bluey_baseboard_battery, test_poll_battery_info_not_hard_off)
{
	chipset_in_state_fake.return_val = 0;
	battery_is_present_fake.return_val = BP_YES;

	poll_battery_info();

	zassert_equal(
		battery_poll_dynamic_info_fake.call_count, 0,
		"battery_poll_dynamic_info should not be called when not hard off");
}

/* Test battery polling when chipset is hard off without battery */
ZTEST(bluey_baseboard_battery, test_poll_battery_info_hard_off_no_battery)
{
	chipset_in_state_fake.return_val = 1;
	battery_is_present_fake.return_val = BP_NO;

	poll_battery_info();

	zassert_equal(
		battery_poll_dynamic_info_fake.call_count, 0,
		"battery_poll_dynamic_info should not be called when battery not present");
}

/* Test battery polling when chipset is hard off with battery present */
ZTEST(bluey_baseboard_battery, test_poll_battery_info_hard_off_with_battery)
{
	chipset_in_state_fake.return_val = 1;
	battery_is_present_fake.return_val = BP_YES;

	poll_battery_info();

	zassert_equal(battery_poll_dynamic_info_fake.call_count, 1,
		      "battery_poll_dynamic_info should be called when battery "
		      "is present in hard off");
}

/* Test battery polling hook on HOOK_CHIPSET_HARD_OFF */
ZTEST(bluey_baseboard_battery, test_board_chipset_hard_off)
{
	soc_change_hook_count = 0;
	board_chipset_hard_off();
	zassert_equal(soc_change_hook_count, 1,
		      "HOOK_BATTERY_SOC_CHANGE should be notified on hard off");

	/* Also verify HOOK_CHIPSET_HARD_OFF triggers the handler */
	soc_change_hook_count = 0;
	hook_notify(HOOK_CHIPSET_HARD_OFF);
	zassert_equal(soc_change_hook_count, 1,
		      "HOOK_CHIPSET_HARD_OFF should notify battery SOC change");
}

/* Test battery static information caching during board initialization */
ZTEST(bluey_baseboard_battery, test_board_battery_init)
{
	/* Trigger HOOK_INIT with battery present */
	battery_is_present_fake.return_val = BP_YES;
	hook_notify(HOOK_INIT);
	zassert_true(
		update_static_battery_info_fake.call_count > 0,
		"update_static_battery_info should be called when battery present");

	RESET_FAKE(update_static_battery_info);
	battery_is_present_fake.return_val = BP_NO;
	hook_notify(HOOK_INIT);
	zassert_equal(
		update_static_battery_info_fake.call_count, 0,
		"update_static_battery_info should not be called when battery not present");
}

/* Test board chipset pre-init hook for battery setup */
ZTEST(bluey_baseboard_battery, test_board_chipset_pre_init)
{
	battery_is_present_fake.return_val = BP_NO;
	board_chipset_pre_init();
	zassert_equal(
		battery_poll_dynamic_info_fake.call_count, 0,
		"battery_poll_dynamic_info should not be called when battery not present");

	battery_is_present_fake.return_val = BP_YES;
	board_chipset_pre_init();
	zassert_equal(
		battery_poll_dynamic_info_fake.call_count, 1,
		"battery_poll_dynamic_info should be called when battery present");
}

/* Test battery access limit checking restricted to G3 state */
ZTEST(bluey_baseboard_battery, test_battery_check_access_limit)
{
	chipset_in_state_fake.return_val = 0;
	zassert_equal(battery_check_access_limit(), BATTERY_ACCESS_NOT_ALLOWED,
		      "Access should not be allowed when not in hard off");

	chipset_in_state_fake.return_val = 1;
	zassert_equal(battery_check_access_limit(), BATTERY_ACCESS_ALLOWED,
		      "Access should be allowed when in hard off");
}

/* Test display state of charge scaling and compensation */
ZTEST(bluey_baseboard_battery, test_board_battery_compensate_params)
{
	struct batt_params batt = { 0 };

	/* Test bad state of charge flag */
	batt.flags = BATT_FLAG_BAD_STATE_OF_CHARGE;
	batt.state_of_charge = 50;
	batt.display_charge = 123;
	board_battery_compensate_params(&batt);
	zassert_equal(
		batt.display_charge, 123,
		"display_charge should not change when BAD_STATE_OF_CHARGE");

	/* Test min threshold (<= 4%) */
	batt.flags = 0;
	batt.state_of_charge = 0;
	board_battery_compensate_params(&batt);
	zassert_equal(batt.display_charge, 0);

	batt.state_of_charge = 4;
	board_battery_compensate_params(&batt);
	zassert_equal(batt.display_charge, 0);

	/* Test max threshold (>= 97%) */
	batt.state_of_charge = 97;
	board_battery_compensate_params(&batt);
	zassert_equal(batt.display_charge, 1000);

	batt.state_of_charge = 100;
	board_battery_compensate_params(&batt);
	zassert_equal(batt.display_charge, 1000);

	/* Test intermediate values */
	batt.state_of_charge = 50;
	board_battery_compensate_params(&batt);
	zassert_equal(batt.display_charge, ((50 - 4) * 1000 + 46) / 93);

	batt.state_of_charge = 10;
	board_battery_compensate_params(&batt);
	zassert_equal(batt.display_charge, ((10 - 4) * 1000 + 46) / 93);
}
