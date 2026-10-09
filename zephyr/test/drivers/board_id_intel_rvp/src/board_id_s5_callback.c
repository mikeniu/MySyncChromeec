/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_id_test_common.h"

#include <zephyr/device.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_pwrseq.h>

ZTEST(board_id_intel_rvp_s5_callback, test_callback_inits_deferred_gpios)
{
	/* Avoid an uninitialized CBI read inside the handler. */
	cbi_get_model_id_fake.return_val = EC_ERROR_UNKNOWN;

	/* Exiting S5 into a higher-power state (S3 > S5) runs the callback. */
	send_ap_pwrseq_s5_exit(AP_POWER_STATE_S3);

	/* The callback called device_init() on the strap controller. */
	zassert_true(device_is_ready(rvp_gpio_dev));
}

static void board_id_intel_rvp_s5_callback_before(void *fixture)
{
	ARG_UNUSED(fixture);

	board_id_intel_rvp_reset();

	/*
	 * This suite verifies that the S5-exit callback initializes the
	 * deferred strap controller, so it must start not-ready. Because the
	 * test mutates that state, it lives in its own binary to stay
	 * independent of test execution order.
	 */
	zassert_false(device_is_ready(rvp_gpio_dev));
}

ZTEST_SUITE(board_id_intel_rvp_s5_callback, NULL, NULL,
	    board_id_intel_rvp_s5_callback_before, NULL, NULL);
