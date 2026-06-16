/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "extpower.h"
#include "fakes.h"
#include "hooks.h"
#include "usb_pd.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/ztest.h>

#ifdef CONFIG_PDC_POWER_MGMT_SRC_THROTTLING

extern void pdc_dpm_remove_sink(int port);
extern int test_extpower_present;

/* * Mock data and variables for battery and AC power state
 */
static struct batt_params mock_batt;

const struct batt_params *charger_current_battery_params(void)
{
	return &mock_batt;
}

/* Helper function to simulate time advancement (triggers HOOK_SECOND) */
static void simulate_seconds(int seconds)
{
	for (int i = 0; i < seconds; i++) {
		hook_notify(HOOK_SECOND);
	}
}

/* Test environment initialization */
static void battery_overload_before(void *f)
{
	/* Default safe discharge state: 1000mA, 10V (10W) */
	mock_batt.current = -1000;
	mock_batt.voltage = 10000;
	mock_batt.flags = 0;
	test_extpower_present = 0;

	/* Ensure initial state is not locked */
	simulate_seconds(1);
}

ZTEST_SUITE(src_policy_battery_overload, NULL, NULL, battery_overload_before,
	    NULL, NULL);

/* * Test 1: Throttling logic when discharge current exceeds
 * max-discharge-current-ma (4500mA) Expected behavior: Must persist for 3
 * seconds to trigger 0 budget.
 */
ZTEST(src_policy_battery_overload, test_overload_current_debounce)
{
	/* Set discharge current to 4600mA (> 4500mA) */
	mock_batt.current = -4600;
	mock_batt.voltage = 10000;

	/* After 1 second, should not trigger yet (OVERLOAD_DEBOUNCE_SECONDS =
	 * 3) */
	simulate_seconds(1);
	zassert_true(pd_get_usb_pd_3a_ports() > 0,
		     "Should not throttle immediately");

	/* After 2 more seconds (3 seconds total) */
	simulate_seconds(2);
	zassert_equal(
		0, pd_get_usb_pd_3a_ports(),
		"3A ports should be 0 after 3 seconds of current overload");
}

/* * Test 2: Throttling logic when discharge power exceeds
 * max-discharge-power-mw (41000mW)
 */
ZTEST(src_policy_battery_overload, test_overload_power_trigger)
{
	/* Safe current (4000mA) but high voltage (11V), total power 44000mW (>
	 * 41000mW) */
	mock_batt.current = -4000;
	mock_batt.voltage = 11000;

	/* Persist for 3 seconds */
	simulate_seconds(3);
	zassert_equal(0, pd_get_usb_pd_3a_ports(),
		      "3A ports should be 0 after 3 seconds of power overload");
}

/* * Test 3: Automatically clear overload protection when AC power (extpower) is
 * connected
 */
ZTEST(src_policy_battery_overload, test_recovery_on_extpower)
{
	/* Trigger overload state first */
	mock_batt.current = -5000;
	simulate_seconds(3);
	zassert_equal(0, pd_get_usb_pd_3a_ports(), "Ensure overloaded state");

	/* Simulate AC power connection */
	test_extpower_present = 1;

	/* Trigger HOOK_SECOND to let pdc_monitor_battery_overload process
	 * recovery logic */
	simulate_seconds(1);

	zassert_true(
		pd_get_usb_pd_3a_ports() > 0,
		"3A capacity should be restored when AC power is connected");
}

/* * Test 4: Automatically clear overload protection on device detach
 * (pdc_dpm_remove_sink)
 */
ZTEST(src_policy_battery_overload, test_recovery_on_detach)
{
	/* Trigger overload state first */
	mock_batt.current = -5000;
	simulate_seconds(3);
	zassert_equal(0, pd_get_usb_pd_3a_ports(), "Ensure overloaded state");

	/* Restore safe state to prevent immediate re-triggering after clearing
	 */
	mock_batt.current = -1000;

	/* Simulate device detach (invoke DPM detach handler) */
	pdc_dpm_remove_sink(0);

	zassert_true(pd_get_usb_pd_3a_ports() > 0,
		     "3A capacity should be restored on device detach");
}
#endif
