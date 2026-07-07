/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <cstdint>
#include <ec_commands.h>
#include <fpsensor/fpsensor_state.h>
#include <span>

/* Define the missing FFF mocks for the linker */
FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

constexpr size_t TARGET_SHA256_DIGEST_LENGTH = 32;

/*
 * Manual mock state variables.
 * FFF (Fake Function Framework) requires mock argument types to be
 * default-constructible to support historical call tracking. Because fixed-size
 * std::span types lack a default constructor, we must implement this mock
 * manually rather than using FFF macros.
 */
static int validate_request_call_count = 0;
static enum ec_error_list validate_request_return_val = EC_SUCCESS;

/*
 * Production symbol override.
 *
 * The production instance of validate_request() in the firmware source is
 * annotated with `test_mockable` (which expands to a weak attribute during test
 * builds). This normal (strong) definition cleanly overrides it for this test
 * suite.
 *
 * Note: A manual mock is implemented here instead of using the Fake Function
 * Framework (FFF) because FFF requires mock arguments to be
 * default-constructible to track call history. Fixed-size std::span types do
 * not possess a default constructor.
 */
enum ec_error_list
validate_request(std::span<const uint8_t> context,
		 std::span<const uint8_t> operation,
		 std::span<const uint8_t, TARGET_SHA256_DIGEST_LENGTH> mac)
{
	validate_request_call_count++;
	return validate_request_return_val;
}

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
	 * check rather than a failed crypto verification.
	 */
	validate_request_return_val = EC_SUCCESS;

	/* Submit compound v1 request containing multiple auth-gated bits. */
	int rv = ec_cmd_fp_mode_v1(NULL, &params, &response);

	/* Verify request is blocked by popcount validation check. */
	zassert_equal(
		rv, EC_RES_ACCESS_DENIED,
		"Expected compound auth modes to return EC_RES_ACCESS_DENIED (%d), got %d",
		EC_RES_ACCESS_DENIED, rv);

	/* Verify that the manual mock was NEVER hit, proving popcount blocked
	 * it early */
	zassert_equal(
		validate_request_call_count, 0,
		"Expected validate_request to not be called due to early popcount rejection");
}

static void fpsensor_state_before(void *f)
{
	/* Reset context before each test case */
	global_context.fp_encryption_status = 0;
	global_context.sensor_mode = 0;

	/* Reset manual mock states */
	validate_request_call_count = 0;
	validate_request_return_val = EC_SUCCESS;

	/* Reset remaining FFF mocks */
	RESET_FAKE(mkbp_send_event);
}

ZTEST_SUITE(fpsensor_state, NULL, NULL, fpsensor_state_before, NULL, NULL);
