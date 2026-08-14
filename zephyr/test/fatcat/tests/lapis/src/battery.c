/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "bbram.h"
#include "chipset.h"
#include "extpower.h"
#include "hooks.h"
#include "power_button.h"

#include <zephyr/device.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

/* Must match the definition in lapis battery.c */
#define BATT_KEEP_VALUE 0xA5A5A5A5

/* Functions under test, exported from lapis battery.c via test_export_static */
void check_first_boot(void);
void first_power_on(void);
void update_poweron_flag(void);
int custom_prevent_power_on(void);

/* Non-static function under test */
enum battery_present battery_is_present(void);

/*
 * bbram_read()/bbram_write() are Zephyr syscalls that inline straight
 * through to the bbram device's driver API callbacks, so they cannot be
 * faked at the symbol level. Instead the test registers a mock BBRAM
 * device (chosen as cros-ec,bbram in battery.dtsi) whose API callbacks
 * are these FFF fakes.
 */
FAKE_VALUE_FUNC(int, mock_bbram_read, const struct device *, size_t, size_t,
		uint8_t *);
FAKE_VALUE_FUNC(int, mock_bbram_write, const struct device *, size_t, size_t,
		const uint8_t *);

/* Fakes for the remaining lapis battery.c dependencies */
FAKE_VALUE_FUNC(int, power_button_is_pressed);
FAKE_VALUE_FUNC(int, extpower_is_present);
FAKE_VOID_FUNC(chipset_power_on);

/*
 * Additional chipset symbols pulled in by CONFIG_AP_POWER_CONTROL;
 * linked but never called by these tests.
 */
FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);

/*
 * Fake for sb_read(), used by battery_is_present(). The custom fake lets
 * each test decide how many initial calls fail and what value is returned
 * on a successful read.
 */
FAKE_VALUE_FUNC(int, sb_read, int, int *);

static int sb_read_fail_count;
static int sb_read_state;
static int sb_read_call_count;

static int sb_read_custom(int cmd, int *data)
{
	ARG_UNUSED(cmd);

	if (sb_read_call_count++ < sb_read_fail_count)
		return 1;

	*data = sb_read_state;
	return 0;
}

/* Stub used only to link battery_is_present() (not exercised here) */
int sb_write(int cmd, int data)
{
	ARG_UNUSED(cmd);
	ARG_UNUSED(data);

	return 0;
}

/* Mock BBRAM device wired to the FFF fakes */
static int mock_bbram_init(const struct device *dev)
{
	return 0;
}

static const struct bbram_driver_api mock_bbram_api = {
	.read = mock_bbram_read,
	.write = mock_bbram_write,
};

DEVICE_DT_DEFINE(DT_NODELABEL(mock_bbram), mock_bbram_init, NULL, NULL, NULL,
		 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		 &mock_bbram_api);

/* Emulated BBRAM content controlled by each test */
static uint32_t fake_bbram_content;
static uint32_t fake_bbram_last_written;

static int mock_bbram_read_custom(const struct device *dev, size_t offset,
				  size_t size, uint8_t *data)
{
	memcpy(data, &fake_bbram_content, size);
	return 0;
}

static int mock_bbram_write_custom(const struct device *dev, size_t offset,
				   size_t size, const uint8_t *data)
{
	memcpy(&fake_bbram_last_written, data, size);
	return 0;
}

/*
 * Put the code into a known "first boot" state: BBRAM does not contain
 * BATT_KEEP_VALUE, so check_first_boot() clears poweron_flag.
 */
static void force_first_boot_state(void)
{
	fake_bbram_content = 0;
	check_first_boot();
	RESET_FAKE(mock_bbram_write);
}

/*
 * Put the code into a known "not first boot" state: BBRAM already
 * contains BATT_KEEP_VALUE, so check_first_boot() sets poweron_flag.
 */
static void force_powered_on_state(void)
{
	fake_bbram_content = BATT_KEEP_VALUE;
	check_first_boot();
	RESET_FAKE(mock_bbram_write);
}

static void battery_before(void *data)
{
	ARG_UNUSED(data);

	fake_bbram_content = 0;
	fake_bbram_last_written = 0;

	RESET_FAKE(mock_bbram_read);
	RESET_FAKE(mock_bbram_write);
	RESET_FAKE(power_button_is_pressed);
	RESET_FAKE(extpower_is_present);
	RESET_FAKE(chipset_power_on);
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(chipset_force_shutdown);
	RESET_FAKE(sb_read);

	mock_bbram_read_fake.custom_fake = mock_bbram_read_custom;
	mock_bbram_write_fake.custom_fake = mock_bbram_write_custom;

	sb_read_fail_count = 0;
	sb_read_state = 0;
	sb_read_call_count = 0;
	sb_read_fake.custom_fake = sb_read_custom;
}

ZTEST_SUITE(fatcat_lapis_battery, NULL, NULL, battery_before, NULL, NULL);

ZTEST(fatcat_lapis_battery, test_check_first_boot__first_boot)
{
	fake_bbram_content = 0x00000000;

	check_first_boot();

	zassert_equal(mock_bbram_read_fake.call_count, 1);
	zassert_equal(mock_bbram_read_fake.arg1_val,
		      BBRAM_REGION_OFFSET(board_batt_keep));
	zassert_equal(mock_bbram_read_fake.arg2_val,
		      BBRAM_REGION_SIZE(board_batt_keep));

	/* BATT_KEEP_VALUE must be written back to the same region */
	zassert_equal(mock_bbram_write_fake.call_count, 1);
	zassert_equal(mock_bbram_write_fake.arg1_val,
		      BBRAM_REGION_OFFSET(board_batt_keep));
	zassert_equal(mock_bbram_write_fake.arg2_val,
		      BBRAM_REGION_SIZE(board_batt_keep));
	zassert_equal(fake_bbram_last_written, BATT_KEEP_VALUE);

	/* Power-on stays prevented until button press or AC connect */
	zassert_equal(custom_prevent_power_on(), 1);
}

ZTEST(fatcat_lapis_battery, test_check_first_boot__not_first_boot)
{
	fake_bbram_content = BATT_KEEP_VALUE;

	check_first_boot();

	zassert_equal(mock_bbram_read_fake.call_count, 1);
	zassert_equal(mock_bbram_write_fake.call_count, 0);

	zassert_equal(custom_prevent_power_on(), 0);
}

ZTEST(fatcat_lapis_battery, test_custom_prevent_power_on__power_button)
{
	force_first_boot_state();

	/* Basic check: power on is prevented before the button press */
	power_button_is_pressed_fake.return_val = false;
	zassert_equal(custom_prevent_power_on(), 1);

	/* User presses the power button: power on is now allowed */
	power_button_is_pressed_fake.return_val = true;
	zassert_equal(custom_prevent_power_on(), 0);

	/* The flag is latched: releasing the button keeps allowing it */
	power_button_is_pressed_fake.return_val = false;
	zassert_equal(custom_prevent_power_on(), 0);
}

ZTEST(fatcat_lapis_battery, test_first_power_on__ac_connect)
{
	force_first_boot_state();

	/* AC is connected */
	extpower_is_present_fake.return_val = 1;
	hook_notify(HOOK_AC_CHANGE);

	zassert_equal(chipset_power_on_fake.call_count, 1);

	/* Chipset startup hook marks the flag */
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(custom_prevent_power_on(), 0);
}

ZTEST(fatcat_lapis_battery, test_first_power_on__no_ac)
{
	force_first_boot_state();

	extpower_is_present_fake.return_val = 0;
	hook_notify(HOOK_AC_CHANGE);

	zassert_equal(chipset_power_on_fake.call_count, 0);
	zassert_equal(custom_prevent_power_on(), 1);
}

ZTEST(fatcat_lapis_battery, test_first_power_on__not_first_boot)
{
	force_powered_on_state();

	extpower_is_present_fake.return_val = 1;
	hook_notify(HOOK_AC_CHANGE);

	zassert_equal(chipset_power_on_fake.call_count, 0);
	zassert_equal(custom_prevent_power_on(), 0);
}

ZTEST(fatcat_lapis_battery, test_update_poweron_flag__idempotent)
{
	force_powered_on_state();

	hook_notify(HOOK_CHIPSET_STARTUP);

	zassert_equal(custom_prevent_power_on(), 0);
}

/*
 * battery_is_present() tests.
 *
 * The function talks to the smart battery over I2C via sb_read(). The
 * production code has a static retry counter that is reset on every
 * successful read; the tests below exercise all branches.
 */
ZTEST(fatcat_lapis_battery, test_battery_is_present__success_healthy)
{
	sb_read_state = 0;

	zassert_equal(battery_is_present(), BP_YES);
	zassert_equal(sb_read_fake.call_count, 1);
	zassert_equal(sb_read_fake.arg0_val, SB_MANUFACTURER_ACCESS);
}

ZTEST(fatcat_lapis_battery, test_battery_is_present__success_permanent_failure)
{
	sb_read_state = BIT(12);

	zassert_equal(battery_is_present(), BP_NO);
	zassert_equal(sb_read_fake.call_count, 1);
}

ZTEST(fatcat_lapis_battery, test_battery_is_present__retry_then_healthy)
{
	sb_read_fail_count = 1;
	sb_read_state = 0;

	zassert_equal(battery_is_present(), BP_YES);
	zassert_equal(sb_read_fake.call_count, 2);
}

ZTEST(fatcat_lapis_battery, test_battery_is_present__retry_then_failure)
{
	sb_read_fail_count = 1;
	sb_read_state = BIT(12);

	zassert_equal(battery_is_present(), BP_NO);
	zassert_equal(sb_read_fake.call_count, 2);
}

ZTEST(fatcat_lapis_battery,
      test_battery_is_present__double_fail_under_threshold)
{
	int i;

	/* Make every sb_read() call fail twice in a row. */
	sb_read_fail_count = 1000;

	for (i = 0; i < 100; i++) {
		zassert_equal(battery_is_present(), BP_YES,
			      "iteration %d should report present", i);
	}

	/* After more than 100 consecutive double failures, battery is gone. */
	zassert_equal(battery_is_present(), BP_NO);
}

ZTEST(fatcat_lapis_battery,
      test_battery_is_present__double_fail_resets_after_success)
{
	/* Drive retry_cnt above zero. */
	sb_read_fail_count = 1000;
	zassert_equal(battery_is_present(), BP_YES);

	/* A successful read resets the counter. */
	sb_read_fail_count = 0;
	sb_read_state = 0;
	zassert_equal(battery_is_present(), BP_YES);

	/* Now the double-fail sequence starts from a fresh counter. */
	sb_read_fail_count = 1000;
	zassert_equal(battery_is_present(), BP_YES);
}
