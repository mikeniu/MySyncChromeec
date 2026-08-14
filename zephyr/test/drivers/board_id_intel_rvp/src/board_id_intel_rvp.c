/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_id_test_common.h"

#include <zephyr/device.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_pwrseq.h>
#include <drivers/rvp_board_id.h>

ZTEST(board_id_intel_rvp, test_board_get_version)
{
	/* CBI has no model id yet: board_get_version() reports an error. */
	cbi_get_model_id_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(board_get_version(), -1);

	/* CBI now provides a model id, it is cached and the board id returned.
	 */
	cbi_model_id_value = 0x22a;
	cbi_get_model_id_fake.custom_fake = cbi_get_model_id_match;
	zassert_equal(board_get_version(), 0x22a & BOARD_ID_MASK);
}

ZTEST(board_id_intel_rvp, test_get_rvp_id_config_board_id)
{
	set_board_gpios(0x2a);

	zassert_equal(get_rvp_id_config(BOARD_ID), 0x2a);
}

ZTEST(board_id_intel_rvp, test_get_rvp_id_config_fab_id)
{
	set_fab_gpios(0x1);

	zassert_equal(get_rvp_id_config(FAB_ID), 2);
}

ZTEST(board_id_intel_rvp, test_get_rvp_id_config_bom_id)
{
	set_bom_gpios(0x3);

	zassert_equal(get_rvp_id_config(BOM_ID), 3);
}

ZTEST(board_id_intel_rvp, test_get_rvp_id_config_invalid)
{
	zassert_equal(get_rvp_id_config((enum rvp_id_type)0xff), -1);
}

ZTEST(board_id_intel_rvp, test_rvp_id_handler_stores_in_cbi)
{
	set_board_gpios(0x2a);
	set_fab_gpios(0x1);

	/* CBI read fails, so the freshly computed model id is written back. */
	cbi_get_model_id_fake.return_val = EC_ERROR_UNKNOWN;

	rvp_id_handler();

	zassert_equal(cbi_set_model_id_fake.call_count, 1);
	zassert_equal(cbi_set_model_id_fake.arg0_val,
		      (2 << FAB_ID_SHIFT) | 0x2a);

	/*
	 * After the handler caches the model id, board_get_version() returns
	 * only the board id portion without consulting CBI again.
	 */
	zassert_equal(board_get_version(), 0x2a & BOARD_ID_MASK);
}

ZTEST(board_id_intel_rvp, test_rvp_id_handler_cbi_up_to_date)
{
	set_board_gpios(0x2a);
	set_fab_gpios(0x1);

	/* CBI already holds the same model id, nothing is written. */
	cbi_model_id_value = (2 << FAB_ID_SHIFT) | 0x2a;
	cbi_get_model_id_fake.custom_fake = cbi_get_model_id_match;

	rvp_id_handler();

	zassert_equal(cbi_set_model_id_fake.call_count, 0);
}

ZTEST(board_id_intel_rvp, test_rvp_id_handler_cbi_mismatch)
{
	set_board_gpios(0x2a);
	set_fab_gpios(0x1);

	/* CBI holds a stale model id, so it is refreshed. */
	cbi_model_id_value = 0x999;
	cbi_get_model_id_fake.custom_fake = cbi_get_model_id_match;

	rvp_id_handler();

	zassert_equal(cbi_set_model_id_fake.call_count, 1);
	zassert_equal(cbi_set_model_id_fake.arg0_val,
		      (2 << FAB_ID_SHIFT) | 0x2a);
}

ZTEST(board_id_intel_rvp, test_rvp_id_handler_cbi_set_fails)
{
	set_board_gpios(0x2a);
	set_fab_gpios(0x1);

	/* CBI read fails, so a write is attempted, but the write fails too. */
	cbi_get_model_id_fake.return_val = EC_ERROR_UNKNOWN;
	cbi_set_model_id_fake.return_val = EC_ERROR_UNKNOWN;

	rvp_id_handler();

	zassert_equal(cbi_set_model_id_fake.call_count, 1);
	zassert_equal(cbi_set_model_id_fake.arg0_val,
		      (2 << FAB_ID_SHIFT) | 0x2a);
}

ZTEST(board_id_intel_rvp, test_s5_exit_callback_runs_handler)
{
	set_board_gpios(0x2a);
	set_fab_gpios(0x1);

	/* CBI has no model id yet, so the handler writes the computed one. */
	cbi_get_model_id_fake.return_val = EC_ERROR_UNKNOWN;

	/*
	 * Exiting S5 into a higher-power state (S3 > S5) triggers the driver
	 * callback, which initializes the strap GPIOs and runs the handler.
	 */
	send_ap_pwrseq_s5_exit(AP_POWER_STATE_S3);

	zassert_equal(cbi_set_model_id_fake.call_count, 1);
	zassert_equal(cbi_set_model_id_fake.arg0_val,
		      (2 << FAB_ID_SHIFT) | 0x2a);
}

static void board_id_intel_rvp_before(void *fixture)
{
	ARG_UNUSED(fixture);

	board_id_intel_rvp_reset();

	/*
	 * These tests read known strap values, so make sure the deferred strap
	 * controller is initialized before each test. Doing it here keeps the
	 * per-test helpers free of setup logic.
	 */
	ensure_straps_ready();
}

ZTEST_SUITE(board_id_intel_rvp, NULL, NULL, board_id_intel_rvp_before, NULL,
	    NULL);
