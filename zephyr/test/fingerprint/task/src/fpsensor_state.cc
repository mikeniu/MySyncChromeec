/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "openssl/mem.h"
#include "openssl/rand.h"
#include "sha256.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor_state.h>
#include <span>

/* Define the missing FFF mocks for the linker */
FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

/* Type aliases prevent FFF macro parsing errors caused by template commas. */
using context_span = std::span<const uint8_t>;
using operation_span = std::span<const uint8_t>;

/*
 * Use a dynamic span here. Fixed-size spans lack default constructors,
 * which breaks FFF's historical argument tracking structures.
 */
using mac_span = std::span<const uint8_t>;

FAKE_VALUE_FUNC(enum ec_error_list, validate_request, context_span,
		operation_span, mac_span);

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

	/*
	 * Mock downstream validation to return success. If the request is
	 * still denied, it conclusively proves it was blocked by our popcount
	 * check.
	 */
	validate_request_fake.return_val = EC_SUCCESS;

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
	RESET_FAKE(validate_request);
}

ZTEST_SUITE(fpsensor_state, NULL, NULL, fpsensor_state_before, NULL, NULL);
