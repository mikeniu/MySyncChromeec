/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charger_test.h"
#include "ec_commands.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(charge_manager_seeding_pre_main, charger_predicate_pre_main, NULL,
	    NULL, NULL, NULL);

/**
 * Test that the charge manager starts as un-seeded.
 * This runs before the EC main application.  All charger seeding is expected
 * to happen after the EC main application so that EFS2 runs before
 * most drivers and subsystems initialize.
 */
ZTEST_USER(charge_manager_seeding_pre_main, test_unseeded)
{
	zassert_false(charge_manager_is_seeded());
	zassert_equal(charge_manager_get_available_current(CHARGE_SUPPLIER_PD,
							   0),
		      CHARGE_CURRENT_UNINITIALIZED);
}

ZTEST_SUITE(charge_manager_seeding_post_main, charger_predicate_post_main, NULL,
	    NULL, NULL, NULL);

/**
 * Test that the charge manager starts as un-seeded.
 * This runs before the EC main application.  All charger seeding is expected
 * to happen after the EC main application so that EFS2 runs before
 * most drivers and subsystems initialize.
 */
ZTEST_USER(charge_manager_seeding_post_main, test_seeded)
{
	pdc_power_mgmt_wait_for_sync(0, -1);

#ifdef CONFIG_PLATFORM_EC_DEDICATED_CHARGE_PORT
	struct charge_port_info charge_dc_jack = {
		.current = 3000,
		.voltage = 19500,
	};

	/* When a dedicated charger is configured, the dedicated
	 * charger port must be seeded by board code, so it is expected
	 * that initially the charge manager won't be fully seeded.
	 */
	zassert_false(charge_manager_is_seeded());
	zassert_equal(charge_manager_get_available_current(
			      CHARGE_SUPPLIER_DEDICATED, DEDICATED_CHARGE_PORT),
		      CHARGE_CURRENT_UNINITIALIZED);

	/* Seed the dedciated charger port. */

	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, &charge_dc_jack);
	zassert_equal(charge_manager_get_available_current(
			      CHARGE_SUPPLIER_DEDICATED, DEDICATED_CHARGE_PORT),
		      3000);
#endif

	/* Charger manager expected to be fully seeded. */
	zassert_true(charge_manager_is_seeded());
}
