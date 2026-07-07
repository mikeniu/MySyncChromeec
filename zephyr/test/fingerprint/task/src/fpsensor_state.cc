/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>

/* Define the missing FFF mock for the linker */
FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

ZTEST_USER(fpsensor_state, test_compound_fp_mode_auth_bypass_rejected)
{
	struct ec_params_fp_mode_v1 params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_MATCH,
		.mac = { 0xa5 }, /* Provide MAC payload to satisfy verification
				    check. */
	};
	struct ec_response_fp_mode response;

	/* Force authentication to ensure validation paths are executed. */
	global_context.fp_encryption_status |=
		FP_CONTEXT_STATUS_SESSION_ESTABLISHED;

	/* Submit compound v1 request containing multiple auth-gated bits. */
	int rv = ec_cmd_fp_mode_v1(NULL, &params, &response);

	/* Verify request is blocked by popcount validation check. */
	zassert_equal(
		rv, EC_RES_ACCESS_DENIED,
		"Expected compound auth modes to return EC_RES_ACCESS_DENIED (%d), got %d",
		EC_RES_ACCESS_DENIED, rv);
}

static void fpsensor_state_before(void *f)
{
	/* Reset context before each test case */
	global_context.fp_encryption_status = 0;
	global_context.sensor_mode = 0;

	/* Reset the mock call counts and histories */
	RESET_FAKE(mkbp_send_event);
}

ZTEST_SUITE(fpsensor_state, NULL, NULL, fpsensor_state_before, NULL, NULL);
