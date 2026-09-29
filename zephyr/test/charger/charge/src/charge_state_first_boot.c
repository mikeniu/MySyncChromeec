/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bbram.h"
#include "charge_state.h"
#include "charger_test.h"
#include "chipset.h"
#include "hooks.h"
#include "power_button.h"

#include <zephyr/ztest.h>

/* Must match the magic value used by charge_state.c first boot logic */
#define BATT_KEEP_VALUE 0xA5A5A5A5

/* Exported by charge_state.c when building for ztest */
extern void check_first_boot(void);

/* Controllable extpower stub defined in charge/src/extpower.c */
extern int extpower_present;

static const struct device *const bbram_dev =
	DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram));

/* Stubs for functions invoked by the first boot power-on logic */
static int chipset_power_on_count;

void chipset_power_on(void)
{
	chipset_power_on_count++;
}

int chipset_in_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_HARD_OFF;
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	ARG_UNUSED(reason);
}

static int power_button_pressed;

int power_button_is_pressed(void)
{
	return power_button_pressed;
}

/*
 * Clear the BBRAM keep region then run the init hook, so every test
 * starts in the "first boot" state (poweron_flag == 0).
 */
static void reset_to_first_boot(void)
{
	uint32_t cleared = 0;

	zassert_ok(bbram_write(bbram_dev, BBRAM_REGION_OFFSET(board_batt_keep),
			       BBRAM_REGION_SIZE(board_batt_keep),
			       (uint8_t *)&cleared));
	check_first_boot();
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	chipset_power_on_count = 0;
	power_button_pressed = 0;
	extpower_present = 0;

	reset_to_first_boot();
}

ZTEST_SUITE(charge_state_first_boot, charger_predicate_post_main, NULL, before,
	    NULL, NULL);

ZTEST(charge_state_first_boot, test_first_boot_writes_keep_value)
{
	uint32_t val = 0;

	zassert_ok(bbram_read(bbram_dev, BBRAM_REGION_OFFSET(board_batt_keep),
			      BBRAM_REGION_SIZE(board_batt_keep),
			      (uint8_t *)&val));
	/* The magic value must be persisted for subsequent boots */
	zassert_equal(val, BATT_KEEP_VALUE);
}

ZTEST(charge_state_first_boot, test_first_boot_prevents_power_on)
{
	/* No user interaction yet: power-on is prevented */
	zassert_true(custom_prevent_power_on());
}

ZTEST(charge_state_first_boot, test_subsequent_boot_allows_power_on)
{
	/* Warm boot: BBRAM already holds the keep value */
	check_first_boot();
	zassert_false(custom_prevent_power_on());
}

ZTEST(charge_state_first_boot, test_ac_change_without_extpower)
{
	extpower_present = 0;
	hook_notify(HOOK_AC_CHANGE);
	zassert_equal(chipset_power_on_count, 0);
}

ZTEST(charge_state_first_boot, test_ac_change_powers_on_first_boot)
{
	extpower_present = 1;
	hook_notify(HOOK_AC_CHANGE);
	zassert_equal(chipset_power_on_count, 1);
}

ZTEST(charge_state_first_boot, test_chipset_startup_sets_poweron_flag)
{
	hook_notify(HOOK_CHIPSET_STARTUP);

	/* Power-on is allowed after the chipset started up */
	zassert_false(custom_prevent_power_on());

	/* And AC changes no longer trigger chipset_power_on() */
	extpower_present = 1;
	hook_notify(HOOK_AC_CHANGE);
	zassert_equal(chipset_power_on_count, 0);
}

ZTEST(charge_state_first_boot, test_power_button_allows_power_on)
{
	power_button_pressed = 1;

	/* Power button press bypasses the first boot block */
	zassert_false(custom_prevent_power_on());

	/* The flag is sticky once set by the power button */
	power_button_pressed = 0;
	zassert_false(custom_prevent_power_on());
}
