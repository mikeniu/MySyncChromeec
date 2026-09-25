/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "emul/emul_pdc.h"
#include "extpower.h"
#include "fakes.h"
#include "hooks.h"
#include "src_policy_common.h"
#include "test/util.h"
#include "usb_pd.h"
#include "usbc/pdc_dpm.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#ifdef CONFIG_PDC_POWER_MGMT_SRC_THROTTLING

extern int test_extpower_present;
extern int pdc_dpm_get_source_current(const int port);

#define POLICY_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(usbc_port_policy)
BUILD_ASSERT(DT_PROP(POLICY_NODE, max_discharge_current_ma) == 4500,
	     "Tests assume max-discharge-current-ma is 4500");
BUILD_ASSERT(DT_PROP(POLICY_NODE, max_discharge_power_mw) == 41000,
	     "Tests assume max-discharge-power-mw is 41000");

/* Mock data and variables for battery and AC power state */
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
	k_msleep(100);
}

/* Test environment initialization */
static void reset_battery_state(void)
{
	mock_batt.current = -1000;
	mock_batt.voltage = 10000;
	mock_batt.flags = 0;
	test_extpower_present = 0;
	simulate_seconds(1);
}

static void emul_connect_3a_sink(struct src_policy_fixture *fixture, int port)
{
	union connector_status_t connector_status = { 0 };
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 3000, 0);

	emul_pdc_configure_src(fixture->emul_pdc[port], &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[port], SINK_PDO,
				     PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[port],
					    &connector_status));

	zassert_ok(pdc_power_mgmt_wait_for_sync(port, -1));
}

static void emul_connect_1_5a_sink(struct src_policy_fixture *fixture, int port)
{
	union connector_status_t connector_status = { 0 };
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 1500, 0);

	emul_pdc_configure_src(fixture->emul_pdc[port], &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[port], SINK_PDO,
				     PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[port],
					    &connector_status));

	zassert_ok(pdc_power_mgmt_wait_for_sync(port, -1));
}

static void emul_disconnect_sink(struct src_policy_fixture *fixture, int port)
{
	zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[port]));
	zassert_ok(pdc_power_mgmt_wait_for_sync(port, -1));
}

/* * Test 1: Throttling logic when discharge current exceeds
 * max-discharge-current-ma (4500mA) Expected behavior: Must persist for 3
 * seconds to trigger 0 budget.
 */
ZTEST_USER_F(src_policy, test_overload_current_debounce)
{
	reset_battery_state();
	emul_connect_3a_sink(fixture, TEST_USBC_PORT0);

	zassert_equal(3000, pdc_dpm_get_source_current(TEST_USBC_PORT0));

	mock_batt.current = -4600;
	mock_batt.voltage = 10000;

	simulate_seconds(1);
	zassert_equal(3000, pdc_dpm_get_source_current(TEST_USBC_PORT0));

	simulate_seconds(2);
	zassert_equal(1500, pdc_dpm_get_source_current(TEST_USBC_PORT0));
}

/* * Test 2: Throttling logic when discharge power exceeds
 * max-discharge-power-mw (41000mW)
 */
ZTEST_USER_F(src_policy, test_overload_power_trigger)
{
	reset_battery_state();
	emul_connect_3a_sink(fixture, TEST_USBC_PORT0);
	zassert_equal(3000, pdc_dpm_get_source_current(TEST_USBC_PORT0));

	mock_batt.current = -4000;
	mock_batt.voltage = 11000;

	simulate_seconds(3);
	zassert_equal(1500, pdc_dpm_get_source_current(TEST_USBC_PORT0));
}

/* * Test 3: Automatically clear overload protection when AC power (extpower) is
 * connected
 */
ZTEST_USER_F(src_policy, test_recovery_on_extpower)
{
	reset_battery_state();
	emul_connect_3a_sink(fixture, TEST_USBC_PORT0);

	mock_batt.current = -5000;
	simulate_seconds(3);
	zassert_equal(1500, pdc_dpm_get_source_current(TEST_USBC_PORT0));

	test_extpower_present = 1;
	simulate_seconds(1);
	zassert_equal(3000, pdc_dpm_get_source_current(TEST_USBC_PORT0));
}

/* * Test 4: Automatically clear overload protection on device detach
 * (pdc_dpm_remove_sink)
 */
ZTEST_USER_F(src_policy, test_recovery_on_detach)
{
	reset_battery_state();
	emul_connect_3a_sink(fixture, TEST_USBC_PORT0);

	mock_batt.current = -5000;
	simulate_seconds(3);
	zassert_equal(1500, pdc_dpm_get_source_current(TEST_USBC_PORT0));

	mock_batt.current = -1000;
	emul_disconnect_sink(fixture, TEST_USBC_PORT0);

	emul_connect_3a_sink(fixture, TEST_USBC_PORT0);
	zassert_equal(3000, pdc_dpm_get_source_current(TEST_USBC_PORT0));
}

/* * Test 5: Verify that a new Sink requesting 3A during an active overload
 * is correctly throttled to 1.5A.
 */
ZTEST_USER_F(src_policy, test_throttle_new_sink_during_overload)
{
	reset_battery_state();

	/* Trigger overload state first */
	mock_batt.current = -5000;
	simulate_seconds(3);

	emul_connect_3a_sink(fixture, TEST_USBC_PORT0);
	zassert_equal(1500, pdc_dpm_get_source_current(TEST_USBC_PORT0));
}

/* Test 6: 2 USB-C ports overload and recovery by removing non-3A port */
ZTEST_USER_F(src_policy, test_recovery_on_detach_2_ports)
{
	reset_battery_state();

	/* Port 0: Connect 1.5A sink, verify source current */
	emul_connect_1_5a_sink(fixture, TEST_USBC_PORT0);
	zassert_equal(1500, pdc_dpm_get_source_current(TEST_USBC_PORT0));

	/* Port 1: Connect 3A sink, verify source current */
	emul_connect_3a_sink(fixture, TEST_USBC_PORT1);
	zassert_equal(3000, pdc_dpm_get_source_current(TEST_USBC_PORT1));

	/* Trigger current overload, verify port 1 is 1.5 A */
	mock_batt.current = -5000;
	simulate_seconds(3);
	zassert_equal(1500, pdc_dpm_get_source_current(TEST_USBC_PORT1));

	/* Remove port 0, verify port 1 is now 3A */
	mock_batt.current = -1000;
	emul_disconnect_sink(fixture, TEST_USBC_PORT0);

	simulate_seconds(1);
	zassert_equal(3000, pdc_dpm_get_source_current(TEST_USBC_PORT1));
}
#endif
