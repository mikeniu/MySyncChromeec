/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "charger.h"
#include "ec_commands.h"
#include "host_command.h"
#include "math_util.h"
#include "system.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

void charger_init(void);
int battery_outside_charging_temperature(struct batt_params *batt);
bool battery_sustainer_enabled(void);
enum ec_charge_control_mode get_chg_ctrl_mode(void);
int set_chg_ctrl_mode(enum ec_charge_control_mode mode);
int calculate_sleep_dur(int battery_critical, int sleep_usec);
int charge_request(bool use_curr, bool is_full);
void check_battery_change_soc(bool is_full, bool prev_full);
const struct shell *get_ec_shell(void);
void sustain_battery_soc(void);
void current_limit_battery_soc(void);
void adjust_requested_vi(const struct charger_info *const info, bool is_full);
void battery_sustainer_disable(void);
extern unsigned int user_current_limit;
void wakeup_battery(int *need_static);
void deep_charge_battery(int *need_static);
void revive_battery(int *need_static);
extern timestamp_t precharge_start_time;
extern int battery_seems_dead;
void decide_charge_state(int *need_staticp, int *battery_criticalp);
int shutdown_on_critical_battery(void);
extern timestamp_t shutdown_target_time;
extern int problems_exist;

static enum ec_error_list mock_discharge_on_ac(int chgnum, int enable)
{
	return EC_SUCCESS;
}

/*
 * Strong override: common/battery.c provides a weak definition that queries
 * battery_get_params(), and common/charge_state.c defines it as test_mockable
 * (weak in test builds). Override it here so charge_get_display_charge()
 * reads directly from curr.batt set in the test fixture.
 */
const struct batt_params *charger_current_battery_params(void)
{
	return &charge_get_status()->batt;
}

static struct charger_drv mock_chg_drv;

struct charge_state_fixture {
	struct charge_state_data charge_state_data;
	const struct charger_drv *saved_driver_ptr;
};

static int test_send_host_command(int command, int version, const void *params,
				  size_t params_size, void *resp,
				  size_t resp_size);

static void reset_current_limit(void)
{
	struct ec_params_current_limit_v1 p1 = {
		.limit = -1U,
		.battery_soc = 0,
	};
	test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 1, &p1, sizeof(p1),
			       NULL, 0);
	user_current_limit = -1U;
}

static void *setup(void)
{
	static struct charge_state_fixture fixture;

	fixture.saved_driver_ptr = chg_chips[0].drv;
	mock_chg_drv = *chg_chips[0].drv;
	mock_chg_drv.discharge_on_ac = mock_discharge_on_ac;
	chg_chips[0].drv = &mock_chg_drv;

	return &fixture;
}

static void before(void *f)
{
	struct charge_state_fixture *fixture = f;

	fixture->charge_state_data = *charge_get_status();
	set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL);
	battery_sustainer_disable();
	reset_current_limit();
	battery_seems_dead = 0;
	precharge_start_time.val = 0;
	shutdown_target_time.val = 0;
	problems_exist = 0;
	get_time_mock = NULL;
}

static void after(void *f)
{
	struct charge_state_fixture *fixture = f;

	*charge_get_status() = fixture->charge_state_data;
	set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL);
	battery_sustainer_disable();
	reset_current_limit();
	battery_seems_dead = 0;
	precharge_start_time.val = 0;
	shutdown_target_time.val = 0;
	problems_exist = 0;
	get_time_mock = NULL;
}

static void teardown(void *f)
{
	struct charge_state_fixture *fixture = f;

	chg_chips[0].drv = fixture->saved_driver_ptr;
}

ZTEST_SUITE(charge_state, drivers_predicate_post_main, setup, before, after,
	    teardown);

static int test_send_host_command(int command, int version, const void *params,
				  size_t params_size, void *resp,
				  size_t resp_size)
{
	struct host_cmd_handler_args args = {
		.command = command,
		.version = version,
		.params = params,
		.params_size = params_size,
		.response = resp,
		.response_max = resp_size,
		.response_size = 0,
	};
	return host_command_process(&args);
}

ZTEST(charge_state, test_battery_flag_bad_temperature)
{
	struct charge_state_data *curr = charge_get_status();

	curr->batt.flags |= BATT_FLAG_BAD_TEMPERATURE;
	zassert_ok(battery_outside_charging_temperature(&curr->batt));
}

ZTEST(charge_state, test_battery_temperature_range)
{
	struct charge_state_data *curr = charge_get_status();
	const struct battery_info *batt_info = battery_get_info();

	curr->batt.flags &= ~BATT_FLAG_BAD_TEMPERATURE;

	/* Start off without a desired voltage/current */
	curr->batt.desired_voltage = 0;
	curr->batt.desired_current = 0;

	/* Temperature is too high */
	curr->batt.temperature =
		CELSIUS_TO_DECI_KELVIN(batt_info->start_charging_max_c + 1);
	zassert_equal(1, battery_outside_charging_temperature(&curr->batt));

	/* Temperature is too low */
	curr->batt.temperature =
		CELSIUS_TO_DECI_KELVIN(batt_info->start_charging_min_c - 1);
	zassert_equal(1, battery_outside_charging_temperature(&curr->batt));

	/* Temperature is just right */
	curr->batt.temperature =
		CELSIUS_TO_DECI_KELVIN((batt_info->start_charging_max_c +
					batt_info->start_charging_min_c) /
				       2);
	zassert_ok(battery_outside_charging_temperature(&curr->batt));

	/* Set an arbitrary desired current */
	curr->batt.desired_current = 3;

	/* Temperature is too high */
	curr->batt.temperature =
		CELSIUS_TO_DECI_KELVIN(batt_info->charging_max_c + 1);
	zassert_equal(1, battery_outside_charging_temperature(&curr->batt));

	/* Set an arbitrary desired voltage */
	curr->batt.desired_voltage = 5;

	/* Temperature is too low */
	curr->batt.temperature =
		CELSIUS_TO_DECI_KELVIN(batt_info->charging_min_c - 1);
	zassert_equal(1, battery_outside_charging_temperature(&curr->batt));

	/* Temperature is just right */
	curr->batt.temperature = CELSIUS_TO_DECI_KELVIN(
		(batt_info->charging_max_c + batt_info->charging_min_c) / 2);
	zassert_ok(battery_outside_charging_temperature(&curr->batt));
}

ZTEST(charge_state, test_current_limit_derating)
{
	int charger_current_limit;

	charge_set_input_current_limit(1000, 5000);
	zassert_ok(charger_get_input_current_limit(0, &charger_current_limit));
	/*
	 * ISL923x sets ICL in multiples of 20 mA, so 950 mA gets rounded down
	 * to the nearest multiple of 20.
	 */
	zassert_equal(
		charger_current_limit, 944,
		"%d%% derating of 1A should be 944 mA, but charger is set for %d mA",
		CONFIG_PLATFORM_EC_CHARGER_INPUT_CURRENT_DERATE_PCT,
		charger_current_limit);
}

ZTEST(charge_state, test_minimum_current_limit)
{
	int charger_current_limit;

	charge_set_input_current_limit(50, 5000);
	zassert_ok(charger_get_input_current_limit(0, &charger_current_limit));
	zassert_equal(charger_current_limit, 96,
		      "Minimum input current limit should be %d mA,"
		      " but current limit is %d (capped to %d)",
		      96, charger_current_limit,
		      CONFIG_PLATFORM_EC_CHARGER_MIN_INPUT_CURRENT_LIMIT);
}

/* ---------------- Host Command: EC_CMD_CHARGE_CONTROL ---------------- */

static int battery_sustainer_set_hc(int version, int8_t lower, int8_t upper,
				    enum ec_charge_control_flag flags)
{
	struct ec_params_charge_control p = { 0 };

	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.mode = CHARGE_CONTROL_NORMAL;
	p.sustain_soc.lower = lower;
	p.sustain_soc.upper = upper;
	p.flags = flags;
	return test_send_host_command(EC_CMD_CHARGE_CONTROL, version, &p,
				      sizeof(p), NULL, 0);
}

static int battery_sustainer_get_hc(int version,
				    struct ec_response_charge_control *r)
{
	struct ec_params_charge_control p = { 0 };

	p.cmd = EC_CHARGE_CONTROL_CMD_GET;
	return test_send_host_command(EC_CMD_CHARGE_CONTROL, version, &p,
				      sizeof(p), r, sizeof(*r));
}

ZTEST(charge_state, test_hc_charge_control__v2_and_v3)
{
	struct charge_state_data *curr = charge_get_status();
	struct ec_params_charge_control p = { 0 };
	struct ec_response_charge_control r;
	int rv;

	/* Mode changes require AC present */
	curr->ac = 1;

	/* Test v2 valid sustainer */
	rv = battery_sustainer_set_hc(2, 79, 80, 0);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_true(battery_sustainer_enabled());

	rv = battery_sustainer_get_hc(2, &r);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_equal(79, r.sustain_soc.lower);
	zassert_equal(80, r.sustain_soc.upper);

	/* Test v2 lower > upper */
	rv = battery_sustainer_set_hc(2, 80, 79, 0);
	zassert_equal(EC_RES_INVALID_PARAM, rv);

	/* Test v2 lower < -1 */
	rv = battery_sustainer_set_hc(2, -100, 80, 0);
	zassert_equal(EC_RES_INVALID_PARAM, rv);

	/* Test v2 upper > 100 */
	rv = battery_sustainer_set_hc(2, 79, 101, 0);
	zassert_equal(EC_RES_INVALID_PARAM, rv);

	/* Test v2 lower < upper sets EC_CHARGE_CONTROL_FLAG_NO_IDLE */
	rv = battery_sustainer_set_hc(2, 70, 85, 0);
	zassert_equal(EC_RES_SUCCESS, rv);
	rv = battery_sustainer_get_hc(3, &r);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_equal(70, r.sustain_soc.lower);
	zassert_equal(85, r.sustain_soc.upper);
	zassert_true(r.flags & EC_CHARGE_CONTROL_FLAG_NO_IDLE);

	/* Disable sustainer to reset flags */
	rv = battery_sustainer_set_hc(2, -1, -1, 0);
	zassert_equal(EC_RES_SUCCESS, rv);

	/* Test v2 lower == upper does not set NO_IDLE */
	rv = battery_sustainer_set_hc(2, 80, 80, 0);
	zassert_equal(EC_RES_SUCCESS, rv);
	rv = battery_sustainer_get_hc(3, &r);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_equal(80, r.sustain_soc.lower);
	zassert_equal(80, r.sustain_soc.upper);
	zassert_false(r.flags & EC_CHARGE_CONTROL_FLAG_NO_IDLE);

	/* Disable sustainer */
	rv = battery_sustainer_set_hc(2, -1, -1, 0);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_false(battery_sustainer_enabled());

	/* Test v3 valid sustainer with flags */
	rv = battery_sustainer_set_hc(3, 79, 80,
				      EC_CHARGE_CONTROL_FLAG_NO_IDLE);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_true(battery_sustainer_enabled());

	rv = battery_sustainer_get_hc(3, &r);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_equal(79, r.sustain_soc.lower);
	zassert_equal(80, r.sustain_soc.upper);
	zassert_equal(EC_CHARGE_CONTROL_FLAG_NO_IDLE, r.flags);

	/* Disable sustainer */
	rv = battery_sustainer_set_hc(3, -1, -1, 0);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_false(battery_sustainer_enabled());

	/* Test charge control modes */
	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.sustain_soc.lower = -1;
	p.sustain_soc.upper = -1;
	p.flags = 0;

	p.mode = CHARGE_CONTROL_IDLE;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &p, sizeof(p),
				    NULL, 0);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_equal(CHARGE_CONTROL_IDLE, get_chg_ctrl_mode());

	p.mode = CHARGE_CONTROL_DISCHARGE;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &p, sizeof(p),
				    NULL, 0);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_equal(CHARGE_CONTROL_DISCHARGE, get_chg_ctrl_mode());

	p.mode = CHARGE_CONTROL_NORMAL;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &p, sizeof(p),
				    NULL, 0);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_equal(CHARGE_CONTROL_NORMAL, get_chg_ctrl_mode());

	/* Test illegal command */
	p.cmd = UINT8_MAX;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 3, &p, sizeof(p), &r,
				    sizeof(r));
	zassert_equal(EC_RES_INVALID_PARAM, rv);

	/* Test illegal control mode */
	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	p.mode = CHARGE_CONTROL_COUNT;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 3, &p, sizeof(p), &r,
				    sizeof(r));
	zassert_equal(EC_RES_INVALID_PARAM, rv);

	/* Test buffer too small on GET */
	p.cmd = EC_CHARGE_CONTROL_CMD_GET;
	rv = test_send_host_command(EC_CMD_CHARGE_CONTROL, 3, &p, sizeof(p), &r,
				    sizeof(r) - 1);
	zassert_equal(EC_RES_RESPONSE_TOO_BIG, rv);
}

ZTEST(charge_state, test_charge_control_bbram_save_load)
{
	int8_t lower, upper;
	uint8_t flags;

	/* Save and load valid settings */
	zassert_ok(charge_control_save_to_bbram(
		75, 80, EC_CHARGE_CONTROL_FLAG_NO_IDLE));
	zassert_ok(charge_control_load_from_bbram(&lower, &upper, &flags));
	zassert_equal(75, lower);
	zassert_equal(80, upper);
	zassert_equal(EC_CHARGE_CONTROL_FLAG_NO_IDLE, flags);

	/* Save and load disabled settings (-1, -1) */
	zassert_ok(charge_control_save_to_bbram(-1, -1, 0));
	zassert_ok(charge_control_load_from_bbram(&lower, &upper, &flags));
	zassert_equal(-1, lower);
	zassert_equal(-1, upper);
	zassert_equal(0, flags);

	/* Zeroed BBRAM (uninitialized after battery disconnect) defaults to
	 * disabled */
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_LOWER, 0);
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_UPPER, 0);
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_FLAGS, 0);
	zassert_ok(charge_control_load_from_bbram(&lower, &upper, &flags));
	zassert_equal(-1, lower);
	zassert_equal(-1, upper);
	zassert_equal(0, flags);

	/* All 0xFF BBRAM defaults to disabled */
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_LOWER, 0xFF);
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_UPPER, 0xFF);
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_FLAGS, 0xFF);
	zassert_ok(charge_control_load_from_bbram(&lower, &upper, &flags));
	zassert_equal(-1, lower);
	zassert_equal(-1, upper);
	zassert_equal(0, flags);

	/* Invalid BBRAM (lower > upper) defaults to disabled */
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_LOWER, 80);
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_UPPER, 70);
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_FLAGS, 0);
	zassert_ok(charge_control_load_from_bbram(&lower, &upper, &flags));
	zassert_equal(-1, lower);
	zassert_equal(-1, upper);
	zassert_equal(0, flags);

	/* Invalid BBRAM (upper > 100) defaults to disabled */
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_LOWER, 50);
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_UPPER, 105);
	system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_FLAGS, 0);
	zassert_ok(charge_control_load_from_bbram(&lower, &upper, &flags));
	zassert_equal(-1, lower);
	zassert_equal(-1, upper);
	zassert_equal(0, flags);
}

ZTEST(charge_state, test_charge_control_persistence_across_init)
{
	struct charge_state_data *curr = charge_get_status();
	struct ec_response_charge_control r;
	uint8_t raw_val;

	curr->ac = 1;

	/* Set sustainer via host command */
	zassert_ok(battery_sustainer_set_hc(3, 75, 80,
					    EC_CHARGE_CONTROL_FLAG_NO_IDLE));
	zassert_true(battery_sustainer_enabled());

	/* Verify BBRAM was updated */
	zassert_ok(
		system_get_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_LOWER, &raw_val));
	zassert_equal(75, (int8_t)raw_val);
	zassert_ok(
		system_get_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_UPPER, &raw_val));
	zassert_equal(80, (int8_t)raw_val);
	zassert_ok(
		system_get_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_FLAGS, &raw_val));
	zassert_equal(EC_CHARGE_CONTROL_FLAG_NO_IDLE, raw_val);

	/* Simulate EC reboot / re-initialization */
	charger_init();

	/* Verify sustainer is restored from BBRAM */
	zassert_true(battery_sustainer_enabled());
	zassert_ok(battery_sustainer_get_hc(3, &r));
	zassert_equal(75, r.sustain_soc.lower);
	zassert_equal(80, r.sustain_soc.upper);
	zassert_equal(EC_CHARGE_CONTROL_FLAG_NO_IDLE, r.flags);

	/* Disable sustainer via host command */
	zassert_ok(battery_sustainer_set_hc(3, -1, -1, 0));
	zassert_false(battery_sustainer_enabled());

	/* Verify BBRAM was updated with disabled state */
	zassert_ok(
		system_get_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_LOWER, &raw_val));
	zassert_equal((uint8_t)-1, raw_val);
	zassert_ok(
		system_get_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_UPPER, &raw_val));
	zassert_equal((uint8_t)-1, raw_val);

	/* Simulate EC reboot / re-initialization */
	charger_init();

	/* Verify sustainer remains disabled */
	zassert_false(battery_sustainer_enabled());
	zassert_ok(battery_sustainer_get_hc(3, &r));
	zassert_equal(-1, r.sustain_soc.lower);
	zassert_equal(-1, r.sustain_soc.upper);
}

ZTEST(charge_state, test_battery_sustainer_state_machine)
{
	struct charge_state_data *curr = charge_get_status();

	curr->ac = 1;
	curr->batt.is_present = BP_YES;

	/* Sustainer disabled: sustain_battery_soc does nothing */
	zassert_ok(battery_sustainer_set_hc(3, -1, -1, 0));
	zassert_ok(set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL));
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_NORMAL, get_chg_ctrl_mode());

	/* Enable sustainer [70, 80] with idle allowed */
	zassert_ok(battery_sustainer_set_hc(3, 70, 80, 0));

	/* In NORMAL mode: */
	/* soc < upper (75%): remains NORMAL */
	curr->batt.display_charge = 750;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_NORMAL, get_chg_ctrl_mode());

	/* soc == upper (80%): switches to IDLE */
	curr->batt.display_charge = 800;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_IDLE, get_chg_ctrl_mode());

	/* Reset to NORMAL mode, soc > upper (85%): switches to DISCHARGE */
	zassert_ok(set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL));
	curr->batt.display_charge = 850;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_DISCHARGE, get_chg_ctrl_mode());

	/* In IDLE mode: */
	zassert_ok(set_chg_ctrl_mode(CHARGE_CONTROL_IDLE));

	/* soc between lower and upper (75%): remains IDLE */
	curr->batt.display_charge = 750;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_IDLE, get_chg_ctrl_mode());

	/* soc > upper (85%): switches to DISCHARGE */
	curr->batt.display_charge = 850;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_DISCHARGE, get_chg_ctrl_mode());

	/* Back to IDLE, soc < lower (65%): switches to NORMAL */
	zassert_ok(set_chg_ctrl_mode(CHARGE_CONTROL_IDLE));
	curr->batt.display_charge = 650;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_NORMAL, get_chg_ctrl_mode());

	/* In DISCHARGE mode: */
	zassert_ok(set_chg_ctrl_mode(CHARGE_CONTROL_DISCHARGE));

	/* soc > upper (85%): remains DISCHARGE */
	curr->batt.display_charge = 850;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_DISCHARGE, get_chg_ctrl_mode());

	/* soc <= upper (75%): switches to IDLE */
	curr->batt.display_charge = 750;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_IDLE, get_chg_ctrl_mode());

	/* Enable sustainer [70, 80] with NO_IDLE flag */
	zassert_ok(battery_sustainer_set_hc(3, 70, 80,
					    EC_CHARGE_CONTROL_FLAG_NO_IDLE));

	/* In DISCHARGE mode with NO_IDLE, soc <= upper (75%): stays DISCHARGE
	 */
	zassert_ok(set_chg_ctrl_mode(CHARGE_CONTROL_DISCHARGE));
	curr->batt.display_charge = 750;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_DISCHARGE, get_chg_ctrl_mode());

	/* In DISCHARGE mode with NO_IDLE, soc < lower (65%): switches to NORMAL
	 */
	curr->batt.display_charge = 650;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_NORMAL, get_chg_ctrl_mode());

	/* In NORMAL mode with NO_IDLE, soc == upper (80%): switches to
	 * DISCHARGE
	 */
	curr->batt.display_charge = 800;
	sustain_battery_soc();
	zassert_equal(CHARGE_CONTROL_DISCHARGE, get_chg_ctrl_mode());
}

ZTEST(charge_state, test_current_limit_battery_soc)
{
	struct charge_state_data *curr = charge_get_status();
	struct ec_params_current_limit_v1 p1 = {
		.limit = 1500,
		.battery_soc = 80,
	};
	const struct charger_info *info = charger_get_info();

	curr->ac = 1;
	curr->state = ST_CHARGE;
	curr->requested_current = 2000;

	/* Set v1 current limit: 1500 mA when soc >= 80% */
	zassert_ok(test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 1, &p1,
					  sizeof(p1), NULL, 0));

	/* When display charge < 80%: limit not applied */
	curr->batt.display_charge = 700;
	curr->requested_current = 2000;
	current_limit_battery_soc();
	adjust_requested_vi(info, false);
	zassert_equal(charger_closest_current(2000), curr->requested_current);

	/* When display charge >= 80%: limit applied */
	curr->batt.display_charge = 850;
	curr->requested_current = 2000;
	current_limit_battery_soc();
	adjust_requested_vi(info, false);
	zassert_equal(charger_closest_current(1500), curr->requested_current);

	/* Remove limit */
	p1.limit = -1U;
	p1.battery_soc = 0;
	zassert_ok(test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 1, &p1,
					  sizeof(p1), NULL, 0));
	curr->requested_current = 2000;
	current_limit_battery_soc();
	adjust_requested_vi(info, false);
	zassert_equal(charger_closest_current(2000), curr->requested_current);
	zassert_equal(-1U, user_current_limit);
}

/* ---------------- Host Command: EC_CMD_CHARGE_CURRENT_LIMIT ----------------
 */

ZTEST(charge_state, test_hc_charge_current_limit)
{
	struct ec_params_current_limit p0 = { 0 };
	struct ec_params_current_limit_v1 p1 = { 0 };
	int rv;

	/* v0 current limit */
	p0.limit = 2000;
	rv = test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 0, &p0,
				    sizeof(p0), NULL, 0);
	zassert_equal(EC_RES_SUCCESS, rv);

	/* v0 remove limit */
	p0.limit = -1U;
	rv = test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 0, &p0,
				    sizeof(p0), NULL, 0);
	zassert_equal(EC_RES_SUCCESS, rv);

	/* v1 current limit */
	p1.limit = 2000;
	p1.battery_soc = 80;
	rv = test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 1, &p1,
				    sizeof(p1), NULL, 0);
	zassert_equal(EC_RES_SUCCESS, rv);

	/* v1 remove limit */
	p1.limit = -1U;
	p1.battery_soc = 0;
	rv = test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 1, &p1,
				    sizeof(p1), NULL, 0);
	zassert_equal(EC_RES_SUCCESS, rv);

	/* v1 invalid battery_soc > 100 */
	p1.battery_soc = 101;
	rv = test_send_host_command(EC_CMD_CHARGE_CURRENT_LIMIT, 1, &p1,
				    sizeof(p1), NULL, 0);
	zassert_equal(EC_RES_INVALID_PARAM, rv);
}

/* ---------------- Host Command: EC_CMD_CHARGE_STATE ---------------- */

ZTEST(charge_state, test_hc_charge_state)
{
	struct charge_state_data *curr = charge_get_status();
	struct ec_params_charge_state params = { 0 };
	struct ec_response_charge_state resp;
	int rv;
	uint32_t tmp;

	curr->ac = 1;

	/* GET_STATE */
	memset(&resp, 0, sizeof(resp));
	params.cmd = CHARGE_STATE_CMD_GET_STATE;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	zassert_equal(EC_RES_SUCCESS, rv);

	/* Check GET_PARAM and SET_PARAM across base parameters */
	for (int i = 0; i < CS_NUM_BASE_PARAMS; i++) {
		memset(&resp, 0, sizeof(resp));
		params.cmd = CHARGE_STATE_CMD_GET_PARAM;
		params.get_param.param = i;
		rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
					    sizeof(params), &resp,
					    sizeof(resp));
		if (!IS_ENABLED(
			    CONFIG_PLATFORM_EC_CHARGER_HYBRID_POWER_BOOST) &&
		    (i == CS_PARAM_CHG_MIN_REQUIRED_MV ||
		     i == CS_PARAM_CHG_IS_ADAPTER_SUFFICIENT)) {
			zassert_equal(EC_RES_INVALID_PARAM, rv,
				      "Param %d should be invalid", i);
			continue;
		}
		zassert_equal(EC_RES_SUCCESS, rv, "Param %d failed get: %d", i,
			      rv);

		tmp = resp.get_param.value;
		switch (i) {
		case CS_PARAM_CHG_VOLTAGE:
		case CS_PARAM_CHG_CURRENT:
		case CS_PARAM_CHG_INPUT_CURRENT:
			tmp = (tmp > 128) ? tmp - 128 : tmp + 128;
			break;
		default:
			break;
		}

		params.cmd = CHARGE_STATE_CMD_SET_PARAM;
		params.set_param.param = i;
		params.set_param.value = tmp;
		rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
					    sizeof(params), &resp,
					    sizeof(resp));

		if (i == CS_PARAM_CHG_STATUS ||
		    (CS_PARAM_LIMIT_POWER <= i && i < CS_NUM_BASE_PARAMS)) {
			zassert_equal(EC_RES_ACCESS_DENIED, rv,
				      "Param %d was writable", i);
		} else if (i == CS_PARAM_CHG_VOLTAGE ||
			   i == CS_PARAM_CHG_CURRENT ||
			   i == CS_PARAM_CHG_INPUT_CURRENT) {
			zassert_equal(EC_RES_SUCCESS, rv,
				      "Param %d failed write: %d", i, rv);
		}
	}

	/* Parameter out of range */
	params.cmd = CHARGE_STATE_CMD_GET_PARAM;
	params.get_param.param = CS_NUM_BASE_PARAMS;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	zassert_equal(EC_RES_INVALID_PARAM, rv);

	params.cmd = CHARGE_STATE_CMD_SET_PARAM;
	params.set_param.param = CS_NUM_BASE_PARAMS;
	params.set_param.value = 0x1000;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	zassert_equal(EC_RES_INVALID_PARAM, rv);

	/* Command out of range */
	params.cmd = CHARGE_STATE_NUM_CMDS;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	zassert_equal(EC_RES_INVALID_PARAM, rv);
}

/* ---------------- Helper Getters & State Overrides ---------------- */

ZTEST(charge_state, test_charge_get_functions)
{
	struct charge_state_data *curr = charge_get_status();
	int temp_k = 0;

	curr->batt.state_of_charge = 65;
	zassert_equal(65, charge_get_percent());

	curr->batt.temperature = CELSIUS_TO_DECI_KELVIN(25);
	curr->batt.flags = 0;
	zassert_ok(charge_get_battery_temp(0, &temp_k));
	zassert_equal(298, temp_k);

	/* Battery temp error flag returns EC_ERROR_UNKNOWN */
	curr->batt.flags = BATT_FLAG_BAD_TEMPERATURE;
	zassert_equal(EC_ERROR_UNKNOWN, charge_get_battery_temp(0, &temp_k));
	curr->batt.flags = 0;

	/* charge_want_shutdown returns true in ST_DISCHARGE when soc is low */
	curr->state = ST_DISCHARGE;
	curr->batt.flags = 0;
	curr->batt.state_of_charge = 1;
	zassert_true(charge_want_shutdown());

	curr->batt.state_of_charge = 50;
	zassert_false(charge_want_shutdown());

	curr->state = ST_CHARGE;
	zassert_false(charge_want_shutdown());

	/* charge_get_state */
	zassert_equal(ST_CHARGE, charge_get_state());

	curr->ac = 1;
	uint32_t flags = charge_get_led_flags();
	zassert_true(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
}

ZTEST(charge_state, test_manual_voltage_and_current)
{
	struct ec_params_charge_state params = { 0 };
	struct ec_response_charge_state resp = { 0 };
	struct ec_params_charge_control ctrl_p = {
		.cmd = EC_CHARGE_CONTROL_CMD_SET,
		.mode = CHARGE_CONTROL_NORMAL,
		.sustain_soc = { .lower = -1, .upper = -1 },
	};
	struct charge_state_data *curr = charge_get_status();

	curr->ac = 1;

	chgstate_set_manual_voltage(8400);
	chgstate_set_manual_current(2000);

	/* Verify manual voltage via debug get_param */
	params.cmd = CHARGE_STATE_CMD_GET_PARAM;
	params.get_param.param = CS_PARAM_DEBUG_MANUAL_VOLTAGE;
	zassert_ok(test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
					  sizeof(params), &resp, sizeof(resp)));
	zassert_equal(8400, (int)resp.get_param.value);

	/* Verify manual current via debug get_param */
	memset(&resp, 0, sizeof(resp));
	params.get_param.param = CS_PARAM_DEBUG_MANUAL_CURRENT;
	zassert_ok(test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
					  sizeof(params), &resp, sizeof(resp)));
	zassert_equal(2000, (int)resp.get_param.value);

	/* Reset manual settings via normal charge control mode */
	zassert_ok(test_send_host_command(EC_CMD_CHARGE_CONTROL, 2, &ctrl_p,
					  sizeof(ctrl_p), NULL, 0));

	memset(&resp, 0, sizeof(resp));
	params.get_param.param = CS_PARAM_DEBUG_MANUAL_VOLTAGE;
	zassert_ok(test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
					  sizeof(params), &resp, sizeof(resp)));
	zassert_equal(-1, (int)resp.get_param.value);

	memset(&resp, 0, sizeof(resp));
	params.get_param.param = CS_PARAM_DEBUG_MANUAL_CURRENT;
	zassert_ok(test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
					  sizeof(params), &resp, sizeof(resp)));
	zassert_equal(-1, (int)resp.get_param.value);
}

ZTEST(charge_state, test_hc_charge_state_debug_params)
{
	struct ec_params_charge_state params = { 0 };
	struct ec_response_charge_state resp;
	int rv;

	int debug_params[] = {
		CS_PARAM_DEBUG_CTL_MODE,
		CS_PARAM_DEBUG_MANUAL_CURRENT,
		CS_PARAM_DEBUG_MANUAL_VOLTAGE,
		CS_PARAM_DEBUG_SEEMS_DEAD,
		CS_PARAM_DEBUG_SEEMS_DISCONNECTED,
	};

	for (size_t i = 0; i < ARRAY_SIZE(debug_params); i++) {
		memset(&resp, 0, sizeof(resp));
		params.cmd = CHARGE_STATE_CMD_GET_PARAM;
		params.get_param.param = debug_params[i];
		rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
					    sizeof(params), &resp,
					    sizeof(resp));
		zassert_equal(EC_RES_SUCCESS, rv, "Debug param %d failed",
			      debug_params[i]);
	}

	/* Invalid debug param CS_PARAM_DEBUG_BATT_REMOVED */
	params.cmd = CHARGE_STATE_CMD_GET_PARAM;
	params.get_param.param = CS_PARAM_DEBUG_BATT_REMOVED;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 0, &params,
				    sizeof(params), &resp, sizeof(resp));
	zassert_not_equal(EC_RES_SUCCESS, rv);
}

ZTEST(charge_state, test_hc_charge_state_v1)
{
	struct ec_params_charge_state params = { 0 };
	struct ec_response_charge_state resp = { 0 };
	int rv;

	params.chgnum = 0;
	params.cmd = CHARGE_STATE_CMD_GET_STATE;
	rv = test_send_host_command(EC_CMD_CHARGE_STATE, 1, &params,
				    sizeof(params), &resp, sizeof(resp));
	zassert_equal(EC_RES_SUCCESS, rv);
}

ZTEST(charge_state, test_led_pwr_get_state)
{
	struct charge_state_data *curr = charge_get_status();

	/* ST_IDLE without battery */
	curr->state = ST_IDLE;
	curr->batt.is_present = BP_NO;
	zassert_equal(LED_PWRS_ERROR, led_pwr_get_state());

	/* ST_IDLE normal */
	curr->batt.is_present = BP_YES;
	curr->ac = 1;
	zassert_equal(LED_PWRS_IDLE, led_pwr_get_state());

	/* ST_DISCHARGE */
	curr->state = ST_DISCHARGE;
	curr->ac = 0;
	zassert_equal(LED_PWRS_DISCHARGE, led_pwr_get_state());

	/* ST_CHARGE with CHARGE_PORT_NONE */
	curr->state = ST_CHARGE;
	curr->ac = 1;
	curr->batt.state_of_charge = 50;
	zassert_equal(LED_PWRS_DISCHARGE, led_pwr_get_state());

	curr->batt.state_of_charge = 100;
	zassert_equal(LED_PWRS_DISCHARGE, led_pwr_get_state());

	/* ST_PRECHARGE normal */
	curr->state = ST_PRECHARGE;
	zassert_equal(LED_PWRS_IDLE, led_pwr_get_state());

	/* ST_PRECHARGE with CHARGE_CONTROL_IDLE -> LED_PWRS_FORCED_IDLE */
	curr->ac = 1;
	zassert_ok(set_chg_ctrl_mode(CHARGE_CONTROL_IDLE));
	curr->state = ST_PRECHARGE;
	zassert_equal(LED_PWRS_FORCED_IDLE, led_pwr_get_state());

	/* Restore to CHARGE_CONTROL_NORMAL */
	zassert_ok(set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL));

	/* Invalid state */
	curr->state = (enum charge_state)99;
	zassert_equal(LED_PWRS_ERROR, led_pwr_get_state());
}

ZTEST(charge_state, test_charge_get_led_flags_and_input_current)
{
	struct charge_state_data *curr = charge_get_status();
	uint32_t flags;

	curr->ac = 1;
	curr->batt.flags = BATT_FLAG_RESPONSIVE;
	flags = charge_get_led_flags();
	zassert_true(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	zassert_true(flags & CHARGE_LED_FLAG_BATT_RESPONSIVE);

	curr->ac = 0;
	curr->batt.flags = 0;
	flags = charge_get_led_flags();
	zassert_false(flags & CHARGE_LED_FLAG_EXTERNAL_POWER);
	zassert_false(flags & CHARGE_LED_FLAG_BATT_RESPONSIVE);

	/* charge_is_consuming_full_input_current (2% < pct < 95%) */
	curr->batt.state_of_charge = 1;
	zassert_false(charge_is_consuming_full_input_current());

	curr->batt.state_of_charge = 50;
	zassert_true(charge_is_consuming_full_input_current());

	curr->batt.state_of_charge = 96;
	zassert_false(charge_is_consuming_full_input_current());
}

ZTEST(charge_state, test_charge_prevent_power_on)
{
	struct charge_state_data *curr = charge_get_status();
	const struct battery_info *batt_info = battery_get_info();

	/* Normal conditions with power button pressed */
	curr->ac = 1;
	curr->batt.is_present = BP_YES;
	curr->batt.state_of_charge = 50;
	curr->batt.flags = 0;
	curr->batt.temperature = CELSIUS_TO_DECI_KELVIN(25);
	zassert_false(charge_prevent_power_on(true));

	/* Battery too hot */
	curr->batt.temperature =
		CELSIUS_TO_DECI_KELVIN(batt_info->discharging_max_c + 5);
	zassert_true(charge_prevent_power_on(false));

	/* Battery too cold when discharging (no AC) */
	curr->ac = 0;
	curr->batt.temperature =
		CELSIUS_TO_DECI_KELVIN(batt_info->discharging_min_c - 5);
	zassert_true(charge_prevent_power_on(false));
}

ZTEST(charge_state, test_charge_request)
{
	struct charge_state_data *curr = charge_get_status();

	curr->ac = 1;
	curr->requested_voltage = 8400;
	curr->requested_current = 2000;
	zassert_ok(charge_request(true, false));

	/* Request with is_full = true */
	zassert_ok(charge_request(false, true));

	/* Request with no voltage/current */
	curr->requested_voltage = 0;
	curr->requested_current = 0;
	zassert_ok(charge_request(true, false));

	/* Request without current and not full (triggers inhibit mode) */
	zassert_ok(charge_request(false, false));
}

ZTEST(charge_state, test_charge_problems)
{
	zassert_equal(0, problems_exist);
	for (int i = 0; i < NUM_PROBLEM_TYPES; i++) {
		charge_problem(i, 1);
		charge_problem(i, 2);
	}
	zassert_equal(1, problems_exist);
}

ZTEST(charge_state, test_calculate_sleep_dur)
{
	struct charge_state_data *curr = charge_get_status();
	int dur;

	/* AC present */
	curr->ac = 1;
	curr->ts = get_time();
	dur = calculate_sleep_dur(0, 0);
	zassert_true(dur >= CHARGE_MIN_SLEEP_USEC);

	/* AC off, discharging */
	curr->ac = 0;
	curr->state = ST_DISCHARGE;
	curr->ts = get_time();
	dur = calculate_sleep_dur(0, 0);
	zassert_true(dur >= CHARGE_MIN_SLEEP_USEC);

	/* Problems exist */
	charge_problem(PR_SET_VOLTAGE, 1);
	dur = calculate_sleep_dur(0, 0);
	zassert_true(dur >= CHARGE_MIN_SLEEP_USEC);

	/* Battery critical with long sleep */
	dur = calculate_sleep_dur(1, 100 * USEC_PER_SEC);
	zassert_true(dur >= CHARGE_MIN_SLEEP_USEC);
}

ZTEST(charge_state, test_battery_level_transitions)
{
	struct charge_state_data *curr = charge_get_status();
	int low_pct = get_battery_threshold_percent(BATT_THRESHOLD_TYPE_LOW);
	int shut_pct =
		get_battery_threshold_percent(BATT_THRESHOLD_TYPE_SHUTDOWN);

	curr->batt.state_of_charge = low_pct + 5;
	curr->batt.flags = 0;
	check_battery_change_soc(false, false);
	zassert_true(charging_progress_displayed());

	/* Calling charging_progress_displayed resets it */
	zassert_false(charging_progress_displayed());

	/* Battery drops below LOW threshold -> transition triggers true */
	curr->batt.state_of_charge = low_pct;
	zassert_true(check_battery_level_transition(BATT_THRESHOLD_TYPE_LOW));

	/* Update prev_charge to current SoC -> no longer a new transition */
	check_battery_change_soc(false, false);
	zassert_false(check_battery_level_transition(BATT_THRESHOLD_TYPE_LOW));

	/* Battery drops below SHUTDOWN threshold -> transition triggers true */
	curr->batt.state_of_charge = shut_pct;
	zassert_true(
		check_battery_level_transition(BATT_THRESHOLD_TYPE_SHUTDOWN));

	check_battery_change_soc(false, false);
	zassert_false(
		check_battery_level_transition(BATT_THRESHOLD_TYPE_SHUTDOWN));
}

ZTEST(charge_state, test_console_cmd_chgstate)
{
	const struct shell *shell = get_ec_shell();
	struct charge_state_data *curr = charge_get_status();

	curr->ac = 1;

	/* Basic dump */
	zassert_ok(shell_execute_cmd(shell, "chgstate"));

	/* Idle mode on/off */
	zassert_ok(shell_execute_cmd(shell, "chgstate idle on"));
	zassert_equal(CHARGE_CONTROL_IDLE, get_chg_ctrl_mode());
	zassert_ok(shell_execute_cmd(shell, "chgstate idle off"));
	zassert_equal(CHARGE_CONTROL_NORMAL, get_chg_ctrl_mode());

	/* Discharge mode on/off */
	zassert_ok(shell_execute_cmd(shell, "chgstate discharge on"));
	zassert_equal(CHARGE_CONTROL_DISCHARGE, get_chg_ctrl_mode());
	zassert_ok(shell_execute_cmd(shell, "chgstate discharge off"));
	zassert_equal(CHARGE_CONTROL_NORMAL, get_chg_ctrl_mode());

	/* Debug mode on/off */
	zassert_ok(shell_execute_cmd(shell, "chgstate debug on"));
	zassert_ok(shell_execute_cmd(shell, "chgstate debug off"));

	/* Sustain mode */
	zassert_ok(shell_execute_cmd(shell, "chgstate sustain 20 80"));
	zassert_true(battery_sustainer_enabled());
	zassert_ok(shell_execute_cmd(shell, "chgstate sustain -1 -1"));
	zassert_false(battery_sustainer_enabled());

	/* Error cases */
	zassert_not_ok(shell_execute_cmd(shell, "chgstate idle"));
	zassert_not_ok(shell_execute_cmd(shell, "chgstate idle invalid"));
	zassert_not_ok(shell_execute_cmd(shell, "chgstate discharge"));
	zassert_not_ok(shell_execute_cmd(shell, "chgstate discharge invalid"));
	zassert_not_ok(shell_execute_cmd(shell, "chgstate debug"));
	zassert_not_ok(shell_execute_cmd(shell, "chgstate debug invalid"));
	zassert_not_ok(shell_execute_cmd(shell, "chgstate sustain 20"));
	zassert_not_ok(shell_execute_cmd(shell, "chgstate sustain bad 80"));
	zassert_not_ok(shell_execute_cmd(shell, "chgstate sustain 20 bad"));
	zassert_not_ok(shell_execute_cmd(shell, "chgstate sustain 80 20"));
	zassert_not_ok(shell_execute_cmd(shell, "chgstate invalid_subcommand"));
}

ZTEST(charge_state, test_wakeup_battery)
{
	struct charge_state_data *curr = charge_get_status();
	const struct battery_info *info = battery_get_info();
	timestamp_t fake_time;
	int need_static = 0;

	fake_time.val = 1000 * USEC_PER_SEC;
	get_time_mock = &fake_time;

	curr->ac = 1;
	curr->batt.flags &= ~BATT_FLAG_RESPONSIVE;
	curr->state = ST_IDLE;
	battery_seems_dead = 0;

	/* 1. First wakeup call starts precharge */
	wakeup_battery(&need_static);
	zassert_equal(1, need_static);
	zassert_equal(ST_PRECHARGE, curr->state);

	/* 2. After precharge delay, requested voltage and current are set */
	need_static = 0;
#ifdef CONFIG_PRECHARGE_DELAY_MS
	fake_time.val += (CONFIG_PRECHARGE_DELAY_MS * USEC_PER_MSEC) + 1;
#else
	fake_time.val += 1;
#endif
	wakeup_battery(&need_static);
	zassert_equal(0, need_static);
	zassert_equal(info->voltage_max, curr->requested_voltage);
	zassert_equal(info->precharge_current, curr->requested_current);

	/* 3. Precharge timeout expires -> battery seems dead */
	need_static = 0;
	fake_time.val += (CONFIG_BATTERY_PRECHARGE_TIMEOUT + 1) * USEC_PER_SEC;
	wakeup_battery(&need_static);
	zassert_equal(1, battery_seems_dead);
	zassert_equal(ST_IDLE, curr->state);
	zassert_equal(0, curr->requested_voltage);
	zassert_equal(0, curr->requested_current);

	/* 4. Dead battery does nothing */
	need_static = 0;
	wakeup_battery(&need_static);
	zassert_equal(0, need_static);
	zassert_equal(ST_IDLE, curr->state);
}

ZTEST(charge_state, test_deep_charge_battery)
{
	struct charge_state_data *curr = charge_get_status();
	const struct battery_info *info = battery_get_info();
	timestamp_t fake_time;
	int need_static = 0;

	fake_time.val = 1000 * USEC_PER_SEC;
	get_time_mock = &fake_time;

	curr->ac = 1;
	curr->state = ST_IDLE;
	curr->batt.flags &= ~BATT_FLAG_DEEP_CHARGE;

	/* 1. Start deep charge */
	deep_charge_battery(&need_static);
	zassert_equal(1, need_static);
	zassert_equal(ST_PRECHARGE, curr->state);
	zassert_true(curr->batt.flags & BATT_FLAG_DEEP_CHARGE);
	zassert_equal(info->voltage_max, curr->requested_voltage);
	zassert_equal(info->precharge_current, curr->requested_current);

	/* 2. Low voltage precharge timeout */
	need_static = 0;
	fake_time.val += CONFIG_BATTERY_LOW_VOLTAGE_TIMEOUT + 100;
	deep_charge_battery(&need_static);
	zassert_equal(ST_IDLE, curr->state);
	zassert_equal(0, curr->requested_voltage);
	zassert_equal(0, curr->requested_current);

	/* 3. ST_IDLE with DEEP_CHARGE flag set */
	deep_charge_battery(&need_static);
	zassert_equal(0, curr->requested_voltage);
	zassert_equal(0, curr->requested_current);
}

ZTEST(charge_state, test_revive_battery)
{
	struct charge_state_data *curr = charge_get_status();
	int need_static = 0;

	/* 1. In ST_PRECHARGE, battery wakes up */
	curr->state = ST_PRECHARGE;
	battery_seems_dead = 0;
	revive_battery(&need_static);
	zassert_equal(1, need_static);
	zassert_equal(0, battery_seems_dead);

	/* 2. Dead battery wakes up */
	curr->state = ST_IDLE;
	battery_seems_dead = 1;
	need_static = 0;
	revive_battery(&need_static);
	zassert_equal(1, need_static);
	zassert_equal(0, battery_seems_dead);
}

ZTEST(charge_state, test_decide_charge_state)
{
	struct charge_state_data *curr = charge_get_status();
	const struct battery_info *info = battery_get_info();
	int need_static = 0, critical = 0;

	curr->ac = 1;
	curr->batt.is_present = BP_YES;
	curr->batt.flags = BATT_FLAG_RESPONSIVE;
	curr->batt.voltage = info->voltage_normal;

	/* Mode not normal -> ST_IDLE */
	set_chg_ctrl_mode(CHARGE_CONTROL_IDLE);
	decide_charge_state(&need_static, &critical);
	zassert_equal(ST_IDLE, curr->state);

	/* Mode normal, unresponsive battery -> wakes battery, needs static */
	set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL);
	curr->state = ST_IDLE;
	need_static = 0;
	curr->batt.flags &= ~BATT_FLAG_RESPONSIVE;
	decide_charge_state(&need_static, &critical);
	zassert_equal(ST_PRECHARGE, curr->state);
	zassert_equal(1, need_static);

	/* Mode normal, responsive battery, low voltage */
	curr->batt.flags |= BATT_FLAG_RESPONSIVE;
	curr->batt.voltage = info->voltage_min;
	decide_charge_state(&need_static, &critical);
	if (IS_ENABLED(CONFIG_BATTERY_LOW_VOLTAGE_PROTECTION))
		zassert_equal(ST_PRECHARGE, curr->state);
	else
		zassert_equal(ST_CHARGE, curr->state);

	/* Mode normal, responsive battery, normal voltage -> ST_CHARGE */
	curr->batt.voltage = info->voltage_normal;
	need_static = 0;
	critical = 0;
	decide_charge_state(&need_static, &critical);
	zassert_equal(ST_CHARGE, curr->state);
	zassert_equal(0, critical);

	/* Critical battery temperature sets critical output flag */
	curr->batt.temperature =
		CELSIUS_TO_DECI_KELVIN(info->discharging_max_c + 10);
	critical = 0;
	decide_charge_state(&need_static, &critical);
	zassert_equal(1, critical);
	curr->batt.temperature = CELSIUS_TO_DECI_KELVIN(25);
	shutdown_target_time.val = 0;

	/* Bad desired voltage/current sets requests to 0 */
	curr->batt.desired_voltage = 8400;
	curr->batt.desired_current = 2000;
	curr->batt.flags |= BATT_FLAG_BAD_DESIRED_VOLTAGE;
	decide_charge_state(&need_static, &critical);
	zassert_equal(0, curr->requested_voltage);
	zassert_equal(0, curr->requested_current);
	curr->batt.flags &= ~BATT_FLAG_BAD_DESIRED_VOLTAGE;

	curr->batt.flags |= BATT_FLAG_BAD_DESIRED_CURRENT;
	decide_charge_state(&need_static, &critical);
	zassert_equal(0, curr->requested_voltage);
	zassert_equal(0, curr->requested_current);
	curr->batt.flags &= ~BATT_FLAG_BAD_DESIRED_CURRENT;

	/* Battery not present -> ST_IDLE, not charging (tested with AC on and
	 * off) */
	curr->batt.is_present = BP_NO;
	curr->ac = 0;
	decide_charge_state(&need_static, &critical);
	zassert_equal(ST_IDLE, curr->state);
	zassert_equal(0, curr->batt_is_charging);

	curr->ac = 1;
	decide_charge_state(&need_static, &critical);
	zassert_equal(ST_IDLE, curr->state);
	zassert_equal(0, curr->batt_is_charging);
	curr->batt.is_present = BP_YES;

	/* No AC -> ST_DISCHARGE */
	curr->ac = 0;
	decide_charge_state(&need_static, &critical);
	zassert_equal(ST_DISCHARGE, curr->state);
	curr->ac = 1;

	/* DEEP_CHARGE flag cleared when voltage returns to normal */
	if (IS_ENABLED(CONFIG_BATTERY_LOW_VOLTAGE_PROTECTION)) {
		curr->batt.flags |= BATT_FLAG_DEEP_CHARGE;
		curr->batt.voltage = info->voltage_normal;
		decide_charge_state(&need_static, &critical);
		zassert_false(curr->batt.flags & BATT_FLAG_DEEP_CHARGE);
	}
}

ZTEST(charge_state, test_shutdown_on_critical_battery)
{
	struct charge_state_data *curr = charge_get_status();
	const struct battery_info *info = battery_get_info();
	timestamp_t fake_time;

	fake_time.val = 1000 * USEC_PER_SEC;
	get_time_mock = &fake_time;

	/* Normal conditions -> returns 0 */
	curr->ac = 1;
	curr->batt_is_charging = 1;
	curr->batt.state_of_charge = 50;
	curr->batt.voltage = 8400;
	curr->batt.temperature = CELSIUS_TO_DECI_KELVIN(25);
	shutdown_target_time.val = 0;
	zassert_equal(0, shutdown_on_critical_battery());

	/* Battery too hot -> critical, starts countdown */
	curr->batt.temperature =
		CELSIUS_TO_DECI_KELVIN(info->discharging_max_c + 10);
	zassert_equal(1, shutdown_on_critical_battery());
	zassert_not_equal(0ULL, shutdown_target_time.val);

	/* Subsequent call before timeout still returns 1 */
	zassert_equal(1, shutdown_on_critical_battery());

	/* Battery cools down -> clears critical condition and resets timer */
	curr->batt.temperature = CELSIUS_TO_DECI_KELVIN(25);
	zassert_equal(0, shutdown_on_critical_battery());
	zassert_equal(0ULL, shutdown_target_time.val);

	/* Battery too cold on AC -> does not trigger shutdown */
	curr->batt.temperature =
		CELSIUS_TO_DECI_KELVIN(info->discharging_min_c - 10);
	curr->ac = 1;
	zassert_equal(0, shutdown_on_critical_battery());

	/* Battery too cold discharging without AC -> triggers shutdown */
	curr->ac = 0;
	zassert_equal(1, shutdown_on_critical_battery());
	zassert_not_equal(0ULL, shutdown_target_time.val);

	/* Reconnecting AC clears cold discharge critical condition */
	curr->ac = 1;
	zassert_equal(0, shutdown_on_critical_battery());
	zassert_equal(0ULL, shutdown_target_time.val);
	curr->batt.temperature = CELSIUS_TO_DECI_KELVIN(25);

	/* Low battery while discharging -> critical, starts countdown */
	curr->batt.state_of_charge = 0;
	curr->batt.voltage = info->voltage_min;
	curr->batt_is_charging = 0;
	zassert_equal(1, shutdown_on_critical_battery());
	zassert_not_equal(0ULL, shutdown_target_time.val);

	/* Active charging clears low battery critical condition */
	curr->batt_is_charging = 1;
	zassert_equal(0, shutdown_on_critical_battery());
	zassert_equal(0ULL, shutdown_target_time.val);
}
