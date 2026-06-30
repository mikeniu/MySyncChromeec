/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_id_test_common.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/ztest.h>

#include <drivers/rvp_board_id.h>

ZTEST(board_id_intel_rvp_not_ready, test_board_id_config_not_ready)
{
	/* The GPIO controller is not ready: report a device error. */
	zassert_equal(get_rvp_id_config(BOARD_ID), -ENODEV);
}

ZTEST(board_id_intel_rvp_not_ready, test_board_id_handler_not_ready)
{
	/*
	 * With the straps unavailable the handler cannot read the board id, so
	 * it bails out without writing CBI.
	 */
	rvp_id_handler();

	zassert_equal(cbi_set_model_id_fake.call_count, 0);
}

static void board_id_intel_rvp_not_ready_before(void *fixture)
{
	ARG_UNUSED(fixture);

	board_id_intel_rvp_reset();

	/*
	 * These tests exercise the device-not-ready paths, so the deferred
	 * strap controller is intentionally left uninitialized. Keeping them in
	 * their own binary guarantees no other test initializes it first.
	 */
	zassert_false(device_is_ready(rvp_gpio_dev));
}

ZTEST_SUITE(board_id_intel_rvp_not_ready, NULL, NULL,
	    board_id_intel_rvp_not_ready_before, NULL, NULL);
