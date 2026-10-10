/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charger_test.h"
#include "drivers/ucsi_v3.h"
#include "ec_commands.h"
#include "emul/emul_pdc.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/ztest.h>

#define RTS5453P_NODE DT_NODELABEL(pdc_emul1)
static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);

ZTEST_SUITE(charge_manager, charger_predicate_post_main, NULL, NULL, NULL,
	    NULL);

/**
 * Test the default implementation of board_fill_source_power_info(). The fill
 * function should reset all the power info values. If the test binary overrides
 * board_fill_source_power_info(), then this test can be removed.
 */
ZTEST_USER(charge_manager, test_default_fill_power_info)
{
	struct ec_response_usb_pd_power_info info = {
		.meas = {
			.voltage_now = 10,
			.voltage_max = 10,
			.current_max = 10,
			.current_lim = 10,
		},
		.max_power = 10,
	};

	board_fill_source_power_info(0, &info);
	zassert_equal(info.meas.voltage_now, 0);
	zassert_equal(info.meas.voltage_max, 0);
	zassert_equal(info.meas.current_max, 0);
	zassert_equal(info.meas.current_lim, 0);
	zassert_equal(info.max_power, 0);
}

/**
 * Test the default implementation of board_charge_port_is_connected(). This
 * function should always return 1 regardless of input.
 */
ZTEST_USER(charge_manager, test_default_charge_port_is_connected)
{
	zassert_true(board_charge_port_is_connected(-1));
	zassert_true(board_charge_port_is_connected(0));
	zassert_true(board_charge_port_is_connected(1));
	zassert_true(board_charge_port_is_connected(500));
}

ZTEST_USER(charge_manager, test_default_charge_port_is_sink)
{
	zassert_true(board_charge_port_is_sink(-1));
	zassert_true(board_charge_port_is_sink(0));
	zassert_true(board_charge_port_is_sink(1));
	zassert_true(board_charge_port_is_sink(500));
}

ZTEST_USER(charge_manager, test_has_no_active_charge_port)
{
	union connector_status_t connector_status = {};

	charge_manager_leave_safe_mode();
	zassert_false(charge_manager_has_active_charge_port());

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	pdc_power_mgmt_wait_for_sync(0, -1);
	zassert_true(charge_manager_has_active_charge_port());

	emul_pdc_disconnect(emul);
	pdc_power_mgmt_wait_for_sync(0, -1);
	zassert_false(charge_manager_has_active_charge_port());
}

ZTEST_USER(charge_manager, test_available_current)
{
	struct charge_port_info charge = {
		.current = 500,
		.voltage = 5000,
	};

	charge_manager_update_charge(CHARGE_SUPPLIER_TYPEC, 0, &charge);
	zassert_equal(
		charge_manager_get_available_current(CHARGE_SUPPLIER_TYPEC, 0),
		500);

	charge.current = 0;
	charge.voltage = 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_TYPEC, 0, &charge);
	zassert_equal(
		charge_manager_get_available_current(CHARGE_SUPPLIER_TYPEC, 0),
		0);

	/* An invalid supplier or port has no current. */
	zassert_equal(charge_manager_get_available_current(CHARGE_SUPPLIER_NONE,
							   0),
		      0);
	zassert_equal(
		charge_manager_get_available_current(CHARGE_SUPPLIER_COUNT, 0),
		0);
	zassert_equal(
		charge_manager_get_available_current(CHARGE_SUPPLIER_TYPEC, -1),
		0);
	zassert_equal(charge_manager_get_available_current(
			      CHARGE_SUPPLIER_TYPEC, CHARGE_PORT_COUNT),
		      0);
}
