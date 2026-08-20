/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_ramp.h"
#include "charger_test.h"
#include "common.h"
#include "console.h"
#include "ec_tasks.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "system_fake.h"
#include "task.h"
#include "timer.h"
#include "usb_charge.h"
#include "util.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define TASK_EVENT_OVERCURRENT (1 << 0)
#define RAMP_STABLE_DELAY (120 * USEC_PER_SEC)
#define CHARGE_DETECT_DELAY_TEST (CHARGE_DETECT_DELAY - 100 * USEC_PER_MSEC)

static int system_load_current_ma;
static int vbus_low_current_ma = 500;
static int overcurrent_current_ma = 3000;
static int charge_limit_ma;

FAKE_VALUE_FUNC(int, system_is_locked);

/* Mock functions */
int chg_ramp_allowed(int port, int supplier)
{
	if (supplier == CHARGE_SUPPLIER_TYPEC_DTS) {
		return !system_is_locked_fake.return_val;
	}
	return supplier >= CHARGE_SUPPLIER_TEST4 &&
	       supplier <= CHARGE_SUPPLIER_TEST8;
}

int chg_ramp_max(int port, int supplier, int sup_curr)
{
	if (supplier == CHARGE_SUPPLIER_TYPEC_DTS)
		return sup_curr;
	if (supplier == CHARGE_SUPPLIER_TEST7)
		return 1600;
	else if (supplier == CHARGE_SUPPLIER_TEST8)
		return 2400;
	else if (supplier >= CHARGE_SUPPLIER_TEST1 &&
		 supplier <= CHARGE_SUPPLIER_TEST8)
		return 3000;
	else
		return 0;
}

struct bc12_config bc12_ports[0];

int charge_is_consuming_full_input_current(void)
{
	return charge_limit_ma <= system_load_current_ma;
}

int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	return MIN(system_load_current_ma, charge_limit_ma) >
	       vbus_low_current_ma;
}

void board_set_charge_limit(int port, int supplier, int limit_ma, int max_ma,
			    int max_mv)
{
	charge_limit_ma = limit_ma;
	if (charge_limit_ma > overcurrent_current_ma)
		task_set_event(TASK_ID_TEST_RUNNER, TASK_EVENT_OVERCURRENT);
}

/* Test utilities */

static void plug_charger_with_ts(int supplier_type, int port, int min_current,
				 int vbus_low_current, int overcurrent_current,
				 timestamp_t reg_time)
{
	vbus_low_current_ma = vbus_low_current;
	overcurrent_current_ma = overcurrent_current;
	chg_ramp_charge_supplier_change(port, supplier_type, min_current,
					reg_time, 0);
}

static void plug_charger(int supplier_type, int port, int min_current,
			 int vbus_low_current, int overcurrent_current)
{
	plug_charger_with_ts(supplier_type, port, min_current, vbus_low_current,
			     overcurrent_current, get_time());
}

static void unplug_charger(void)
{
	chg_ramp_charge_supplier_change(CHARGE_PORT_NONE, CHARGE_SUPPLIER_NONE,
					0, get_time(), 0);
}

static bool unplug_charger_and_check(void)
{
	unplug_charger();
	k_usleep(CHARGE_DETECT_DELAY_TEST);
	return charge_limit_ma == 0;
}

static bool wait_stable_no_overcurrent(void)
{
	return task_wait_event(RAMP_STABLE_DELAY) != TASK_EVENT_OVERCURRENT;
}

static bool is_in_range(int x, int min_val, int max_val)
{
	return x >= min_val && x <= max_val;
}

static void charge_ramp_before(void *state)
{
	ARG_UNUSED(state);
	RESET_FAKE(system_is_locked);
	set_test_runner_tid();
}

static void charge_ramp_after(void *state)
{
	ARG_UNUSED(state);
	RESET_FAKE(system_is_locked);
}

/* Existing tests */

ZTEST_USER(charge_ramp, test_ramp)
{
	zassert_equal(chg_ramp_allowed(0, CHARGE_SUPPLIER_PD), 0);
	zassert_equal(chg_ramp_max(0, CHARGE_SUPPLIER_PD, 1234), 0);

	zassert_equal(chg_ramp_allowed(0, CHARGE_SUPPLIER_TYPEC), 0);
	zassert_equal(chg_ramp_max(0, CHARGE_SUPPLIER_TYPEC, 1234), 0);

	zassert_equal(chg_ramp_allowed(0, CHARGE_SUPPLIER_TYPEC_DTS), 1);
	zassert_equal(chg_ramp_max(0, CHARGE_SUPPLIER_TYPEC_DTS, 1234), 1234);
}

ZTEST_USER(charge_ramp, test_ramp_locked)
{
	enum ec_image old_image = system_get_shrspi_image_copy();

	system_set_shrspi_image_copy(EC_IMAGE_RO);
	zassert_false(system_is_in_rw());

	system_is_locked_fake.return_val = 1;

	zassert_equal(chg_ramp_allowed(0, CHARGE_SUPPLIER_TYPEC_DTS), 0);

	system_set_shrspi_image_copy(old_image);
}

/* Ported tests from test/charge_ramp.c */

ZTEST(charge_ramp, test_no_ramp)
{
	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST1, 0, 500, 3000, 3000);
	k_usleep(CHARGE_DETECT_DELAY_TEST + 200 * USEC_PER_MSEC);
	zassert_equal(charge_limit_ma, 500);
	zassert_true(wait_stable_no_overcurrent());
	zassert_equal(charge_limit_ma, 500);

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_full_ramp)
{
	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 3000);
	k_usleep(CHARGE_DETECT_DELAY_TEST);
	zassert_true(is_in_range(charge_limit_ma, 500, 800));
	zassert_true(wait_stable_no_overcurrent());
	zassert_equal(charge_limit_ma, 3000);

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_vbus_dip)
{
	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST5, 0, 1000, 1500, 1600);

	zassert_true(wait_stable_no_overcurrent());
	zassert_true(is_in_range(charge_limit_ma, 1300, 1500));

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_overcurrent)
{
	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 3000, 1500);
	k_usleep(CHARGE_DETECT_DELAY_TEST);
	zassert_true(is_in_range(charge_limit_ma, 500, 700));

	while (task_wait_event(RAMP_STABLE_DELAY) == TASK_EVENT_OVERCURRENT) {
		unplug_charger();
		k_usleep(USEC_PER_MSEC * 600);
		plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 3000, 1500);
		k_usleep(CHARGE_DETECT_DELAY_TEST);
		zassert_true(is_in_range(charge_limit_ma, 500, 700));
	}

	zassert_true(is_in_range(charge_limit_ma, 1300, 1500));

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_switch_outlet)
{
	int i;

	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 3000, 3000);

	for (i = 0; i < 5; ++i) {
		k_sleep(K_SECONDS(20));
		unplug_charger();
		k_usleep(USEC_PER_MSEC * 1500);
		plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 3000, 3000);
		k_usleep(CHARGE_DETECT_DELAY_TEST);
		zassert_true(is_in_range(charge_limit_ma, 500, 700));
	}

	zassert_true(wait_stable_no_overcurrent());
	zassert_equal(charge_limit_ma, 3000);

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_fast_switch)
{
	int i;

	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 3000);

	for (i = 0; i < 2; ++i) {
		k_sleep(K_SECONDS(20));
		unplug_charger();
		k_usleep(600 * USEC_PER_MSEC);
		plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 3000);
		k_usleep(CHARGE_DETECT_DELAY_TEST);
		zassert_true(is_in_range(charge_limit_ma, 500, 700));
	}

	zassert_true(wait_stable_no_overcurrent());
	zassert_equal(charge_limit_ma, 3000);

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_overcurrent_after_switch_outlet)
{
	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST5, 0, 500, 3000, 1500);
	k_usleep(USEC_PER_SEC * 5);

	unplug_charger();
	k_usleep(USEC_PER_MSEC * 1500);
	plug_charger(CHARGE_SUPPLIER_TEST5, 0, 500, 3000, 1500);

	while (task_wait_event(RAMP_STABLE_DELAY) == TASK_EVENT_OVERCURRENT) {
		unplug_charger();
		k_usleep(USEC_PER_MSEC * 600);
		plug_charger(CHARGE_SUPPLIER_TEST5, 0, 500, 3000, 1500);
		k_usleep(CHARGE_DETECT_DELAY_TEST);
		zassert_true(is_in_range(charge_limit_ma, 500, 700));
	}

	zassert_true(is_in_range(charge_limit_ma, 1300, 1500));

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_partial_load)
{
	system_load_current_ma = 1500;
	plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 2500);

	zassert_true(wait_stable_no_overcurrent());
	zassert_true(is_in_range(charge_limit_ma, 1500, 1600));

	system_load_current_ma = 2000;
	zassert_true(wait_stable_no_overcurrent());
	zassert_true(is_in_range(charge_limit_ma, 2000, 2100));

	system_load_current_ma = 2600;
	while (task_wait_event(RAMP_STABLE_DELAY) == TASK_EVENT_OVERCURRENT) {
		unplug_charger();
		k_usleep(USEC_PER_MSEC * 600);
		plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 2500);
		k_usleep(CHARGE_DETECT_DELAY_TEST);
		zassert_true(is_in_range(charge_limit_ma, 500, 700));
	}

	zassert_true(is_in_range(charge_limit_ma, 2300, 2500));

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_charge_supplier_stable)
{
	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 1500, 1600);
	k_usleep(500 * USEC_PER_MSEC);
	plug_charger(CHARGE_SUPPLIER_TEST2, 0, 3000, 3000, 3000);
	k_usleep(USEC_PER_SEC);
	zassert_equal(charge_limit_ma, 3000);

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_charge_supplier_stable_ramp)
{
	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST3, 0, 500, 3000, 3000);
	k_usleep(500 * USEC_PER_MSEC);
	plug_charger(CHARGE_SUPPLIER_TEST5, 0, 500, 1400, 1500);
	zassert_true(wait_stable_no_overcurrent());
	zassert_true(is_in_range(charge_limit_ma, 1200, 1400));

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_charge_supplier_change)
{
	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 3000);
	zassert_true(wait_stable_no_overcurrent());
	zassert_equal(charge_limit_ma, 3000);

	plug_charger(CHARGE_SUPPLIER_TEST1, 0, 1500, 3000, 3000);
	k_usleep(500 * USEC_PER_MSEC);
	zassert_equal(charge_limit_ma, 1500);
	zassert_true(wait_stable_no_overcurrent());
	zassert_equal(charge_limit_ma, 1500);

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_charge_port_change)
{
	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST5, 0, 500, 1400, 1500);
	zassert_true(wait_stable_no_overcurrent());
	zassert_true(is_in_range(charge_limit_ma, 1200, 1400));

	plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 2000, 2100);
	zassert_true(wait_stable_no_overcurrent());
	zassert_true(is_in_range(charge_limit_ma, 1800, 2000));

	plug_charger(CHARGE_SUPPLIER_TEST1, 0, 2500, 3000, 3000);
	k_usleep(USEC_PER_SEC);
	zassert_equal(charge_limit_ma, 2500);
	zassert_true(wait_stable_no_overcurrent());
	zassert_equal(charge_limit_ma, 2500);

	plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 2000, 2100);
	zassert_true(wait_stable_no_overcurrent());
	zassert_true(is_in_range(charge_limit_ma, 1800, 2000));

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_vbus_shift)
{
	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 1900, 2000);
	zassert_true(wait_stable_no_overcurrent());
	zassert_true(is_in_range(charge_limit_ma, 1700, 1900));

	vbus_low_current_ma = 1800;
	zassert_true(wait_stable_no_overcurrent());
	zassert_true(is_in_range(charge_limit_ma, 1600, 1800));

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_equal_priority_overcurrent)
{
	int overcurrent_count = 0;
	timestamp_t oc_time = get_time();

	system_load_current_ma = 3000;

	while (1) {
		plug_charger_with_ts(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 2000,
				     oc_time);
		oc_time = get_time();
		oc_time.val += 600 * USEC_PER_MSEC;
		if (wait_stable_no_overcurrent())
			break;
		plug_charger_with_ts(CHARGE_SUPPLIER_TEST4, 1, 500, 3000, 2000,
				     oc_time);
		oc_time = get_time();
		oc_time.val += 600 * USEC_PER_MSEC;
		if (wait_stable_no_overcurrent())
			break;
		if (overcurrent_count++ >= 10) {
			unplug_charger();
			zassert_unreachable("In loop waiting for stable state");
		}
	}

	zassert_true(unplug_charger_and_check());
}

ZTEST(charge_ramp, test_ramp_limit)
{
	system_load_current_ma = 3000;

	plug_charger(CHARGE_SUPPLIER_TEST7, 0, 500, 3000, 3000);
	k_usleep(USEC_PER_SEC);
	zassert_true(is_in_range(charge_limit_ma, 500, 700));
	zassert_true(wait_stable_no_overcurrent());
	zassert_equal(charge_limit_ma, 1600);

	plug_charger(CHARGE_SUPPLIER_TEST8, 1, 500, 3000, 3000);
	k_usleep(USEC_PER_SEC);
	zassert_true(is_in_range(charge_limit_ma, 500, 700));
	zassert_true(wait_stable_no_overcurrent());
	zassert_equal(charge_limit_ma, 2400);

	plug_charger(CHARGE_SUPPLIER_TEST7, 0, 500, 1200, 1300);
	k_usleep(USEC_PER_SEC);
	zassert_true(is_in_range(charge_limit_ma, 500, 700));
	zassert_true(wait_stable_no_overcurrent());
	zassert_true(is_in_range(charge_limit_ma, 1000, 1200));

	zassert_true(unplug_charger_and_check());
}

ZTEST_SUITE(charge_ramp, charger_predicate_post_main, NULL, charge_ramp_before,
	    charge_ramp_after, NULL);
