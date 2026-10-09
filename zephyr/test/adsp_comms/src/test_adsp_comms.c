/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adsp_comms.h"
#include "battery.h"
#include "battery_smart.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "stubs.h"
#include "system.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

extern struct i2c_target_config target_cfg;

static void send_adsp_msg(uint8_t fid, uint8_t addr, uint16_t data)
{
	target_cfg.callbacks->write_requested(&target_cfg);
	target_cfg.callbacks->write_received(&target_cfg, addr);
	target_cfg.callbacks->write_received(&target_cfg, fid);
	target_cfg.callbacks->write_received(&target_cfg,
					     (uint8_t)(data & 0xFF));
	target_cfg.callbacks->write_received(&target_cfg, (uint8_t)(data >> 8));
	target_cfg.callbacks->stop(&target_cfg);
	k_msleep(10);
}

static int custom_sb_read(int cmd, int *data)
{
	if (cmd == SB_BATTERY_STATUS) {
		*data = 0x1234;
		return EC_SUCCESS;
	}
	return EC_ERROR_UNKNOWN;
}

static int test_custom_cb_called;
static uint16_t test_custom_cb_data;

static void test_custom_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	test_custom_cb_called++;
	test_custom_cb_data = data;
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_DEFAULT, ADSP_POWER_STATE_REG_RESTART,
		       test_custom_cb);

static void test_before(void *fixture)
{
	stubs_reset();
	test_custom_cb_called = 0;
	test_custom_cb_data = 0;
	hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);
}

/* Test active charge port tracking from ADSP OEM messages */
ZTEST_USER(adsp_comms, test_charge_manager_get_active_charge_port)
{
	zassert_equal(charge_manager_get_active_charge_port(),
		      CHARGE_PORT_NONE);

	/* Set charge port 0 (USB0) */
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_PORT,
		      1);
	zassert_equal(charge_manager_get_active_charge_port(), 0);

	/* Set charge port 1 (USB1) */
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_PORT,
		      2);
	zassert_equal(charge_manager_get_active_charge_port(), 1);

	/* Disable charging */
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_PORT,
		      ADSP_OEM_CUSTOM_CHARGE_PORT_DISABLED);
	zassert_equal(charge_manager_get_active_charge_port(),
		      CHARGE_PORT_NONE);

	/* Invalid charge port */
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_PORT,
		      99);
	zassert_equal(charge_manager_get_active_charge_port(),
		      CHARGE_PORT_NONE);
}

/* Test LED power state machine transitions with and without AC power */
ZTEST_USER(adsp_comms, test_led_pwr_get_state)
{
	/* With AC present */
	extpower_is_present_fake.return_val = 1;

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_CHARGE);
	zassert_equal(led_pwr_get_state(), LED_PWRS_CHARGE);

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_DISCHARGE);
	zassert_equal(led_pwr_get_state(), LED_PWRS_DISCHARGE);

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_ERROR);
	zassert_equal(led_pwr_get_state(), LED_PWRS_ERROR);

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_IDLE);
	zassert_equal(led_pwr_get_state(), LED_PWRS_IDLE);

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_FORCED_IDLE);
	zassert_equal(led_pwr_get_state(), LED_PWRS_FORCED_IDLE);

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_NEAR_FULL);
	zassert_equal(led_pwr_get_state(), LED_PWRS_CHARGE_NEAR_FULL);

	/* Invalid state: remains unchanged */
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      99);
	zassert_equal(led_pwr_get_state(), LED_PWRS_CHARGE_NEAR_FULL);

	/* Without AC present: error remains error, other states become
	 * DISCHARGE */
	extpower_is_present_fake.return_val = 0;

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_ERROR);
	zassert_equal(led_pwr_get_state(), LED_PWRS_ERROR);

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_IDLE);
	zassert_equal(led_pwr_get_state(), LED_PWRS_DISCHARGE);

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_CHARGE);
	zassert_equal(led_pwr_get_state(), LED_PWRS_DISCHARGE);
}

/* Test battery status reporting in ON and OFF chipset states */
ZTEST_USER(adsp_comms, test_battery_status)
{
	int status = 0;

	/* Chipset ANY_OFF: reads via sb_read */
	chipset_in_state_fake.return_val = 1;
	sb_read_fake.custom_fake = custom_sb_read;

	zassert_equal(battery_status(&status), EC_SUCCESS);
	zassert_equal(status, 0x1234);

	sb_read_fake.custom_fake = NULL;
	sb_read_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(battery_status(&status), EC_ERROR_UNKNOWN);

	/* Chipset ON: returns active_battery_status */
	chipset_in_state_fake.return_val = 0;

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM,
		      ADSP_OEM_CUSTOM_REG_BATTERY_STATE, 0x5678);

	zassert_equal(battery_status(&status), EC_SUCCESS);
	zassert_equal(status, 0x5678);
}

/* Test dispatching of various ADSP feature message callbacks */
ZTEST_USER(adsp_comms, test_adsp_callbacks)
{
	/* Custom registered callback invocation check */
	test_custom_cb_called = 0;
	test_custom_cb_data = 0;
	send_adsp_msg(ADSP_FEATURE_DEFAULT, ADSP_POWER_STATE_REG_RESTART,
		      0x5AA5);
	zassert_equal(test_custom_cb_called, 1);
	zassert_equal(test_custom_cb_data, 0x5AA5);

	/* Power state logging callback */
	send_adsp_msg(ADSP_FEATURE_DEFAULT, ADSP_POWER_STATE_REG_VAL, 0x1000);

	/* Magic packet logging callback: valid and invalid */
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_MAGIC,
		      ADSP_OEM_CUSTOM_MAGIC_VAL);
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_MAGIC,
		      0x1234);

	/* Version packet logging callback: valid and invalid */
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_VERSION,
		      ADSP_OEM_CUSTOM_VERSION_1);
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_VERSION,
		      0x02);

	/* Battery level: valid and invalid */
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM,
		      ADSP_OEM_CUSTOM_REG_BATTERY_LEVEL, 75);
	zassert_equal(battery_get_fake_soc(), 75);

	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM,
		      ADSP_OEM_CUSTOM_REG_BATTERY_LEVEL, 105);
	zassert_equal(battery_get_fake_soc(), 75);
}

/* Test 'chgstate' console command across multiple power and charge states */
ZTEST_USER(adsp_comms, test_command_chgstate)
{
	const struct shell *sh = get_ec_shell();

	extpower_is_present_fake.return_val = 1;
	battery_is_present_fake.return_val = BP_YES;
	battery_set_fake_soc(80);

	/* CHARGE state */
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_CHARGE);
	zassert_ok(shell_execute_cmd(sh, "chgstate"));

	/* NEAR_FULL state */
	battery_is_present_fake.return_val = BP_NO;
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_NEAR_FULL);
	zassert_ok(shell_execute_cmd(sh, "chgstate"));

	/* DISCHARGE state */
	battery_is_present_fake.return_val = BP_NOT_SURE;
	extpower_is_present_fake.return_val = 0;
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_DISCHARGE);
	zassert_ok(shell_execute_cmd(sh, "chgstate"));

	/* FORCED_IDLE state */
	battery_is_present_fake.return_val = BP_YES;
	extpower_is_present_fake.return_val = 1;
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_FORCED_IDLE);
	zassert_ok(shell_execute_cmd(sh, "chgstate"));
}

/* Test 'chgstate sustain' console command and BBRAM persistence */
ZTEST_USER(adsp_comms, test_command_chgstate_sustain)
{
	const struct shell *sh = get_ec_shell();
	int8_t lower, upper;
	uint8_t flags;

	/* Set valid sustain limits (lower < upper sets NO_IDLE flag) */
	zassert_ok(shell_execute_cmd(sh, "chgstate sustain 80 85"));
	zassert_ok(charge_control_load_from_bbram(&lower, &upper, &flags));
	zassert_equal(80, lower);
	zassert_equal(85, upper);
	zassert_equal(EC_CHARGE_CONTROL_FLAG_NO_IDLE, flags);
	zassert_ok(shell_execute_cmd(sh, "chgstate"));

	/* Set equal sustain limits (lower == upper does not set NO_IDLE) */
	zassert_ok(shell_execute_cmd(sh, "chgstate sustain 80 80"));
	zassert_ok(charge_control_load_from_bbram(&lower, &upper, &flags));
	zassert_equal(80, lower);
	zassert_equal(80, upper);
	zassert_equal(0, flags);
	zassert_ok(shell_execute_cmd(sh, "chgstate"));

	/* Disable sustain limits with -1 -1 */
	zassert_ok(shell_execute_cmd(sh, "chgstate sustain -1 -1"));
	zassert_ok(charge_control_load_from_bbram(&lower, &upper, &flags));
	zassert_equal(CHARGE_CONTROL_SUSTAINER_DISABLED, lower);
	zassert_equal(CHARGE_CONTROL_SUSTAINER_DISABLED, upper);
	zassert_equal(0, flags);
	zassert_ok(shell_execute_cmd(sh, "chgstate"));

	/* Missing arguments */
	zassert_equal(EC_ERROR_PARAM_COUNT,
		      shell_execute_cmd(sh, "chgstate sustain"));
	zassert_equal(EC_ERROR_PARAM_COUNT,
		      shell_execute_cmd(sh, "chgstate sustain 80"));

	/* Invalid number format */
	zassert_equal(EC_ERROR_PARAM2,
		      shell_execute_cmd(sh, "chgstate sustain abc 80"));
	zassert_equal(EC_ERROR_PARAM3,
		      shell_execute_cmd(sh, "chgstate sustain 80 def"));

	/* Invalid range (lower > upper, upper > 100, lower < -1) */
	zassert_equal(EC_ERROR_INVAL,
		      shell_execute_cmd(sh, "chgstate sustain 85 80"));
	zassert_equal(EC_ERROR_INVAL,
		      shell_execute_cmd(sh, "chgstate sustain -2 80"));
	zassert_equal(EC_ERROR_INVAL,
		      shell_execute_cmd(sh, "chgstate sustain 80 101"));

	/* Unknown sub-command */
	zassert_equal(EC_ERROR_PARAM1, shell_execute_cmd(sh, "chgstate foo"));
}

/* Test reset of ADSP comms state on HOOK_CHIPSET_SHUTDOWN_COMPLETE */
ZTEST_USER(adsp_comms, test_adsp_comms_shutdown_reset)
{
	int status = 0;

	/* Setup active values */
	battery_set_fake_soc(50);
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_PORT,
		      1);
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		      ADSP_OEM_CUSTOM_CHARGE_STATE_CHARGE);
	send_adsp_msg(ADSP_FEATURE_OEM_CUSTOM,
		      ADSP_OEM_CUSTOM_REG_BATTERY_STATE, 0x1111);

	zassert_equal(battery_get_fake_soc(), 50);
	zassert_equal(charge_manager_get_active_charge_port(), 0);

	/* Trigger shutdown reset hook */
	hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

	zassert_equal(battery_get_fake_soc(), -1);
	zassert_equal(charge_manager_get_active_charge_port(),
		      CHARGE_PORT_NONE);

	extpower_is_present_fake.return_val = 1;
	zassert_equal(led_pwr_get_state(), LED_PWRS_IDLE);

	chipset_in_state_fake.return_val = 0;
	zassert_equal(battery_status(&status), EC_SUCCESS);
	zassert_equal(status, 0);
}

ZTEST_SUITE(adsp_comms, NULL, NULL, test_before, NULL, NULL);
