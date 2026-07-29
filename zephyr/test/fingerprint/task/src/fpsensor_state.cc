/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock_fingerprint_algorithm.h"

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

ZTEST_USER(fpsensor_state, test_single_fp_mode_auth_accepted)
{
	struct ec_params_fp_mode_v1 params = {
		.mode = FP_MODE_MATCH,
		.mac = { 0xa5 }, /* Provide MAC payload to satisfy verification
				    check. */
	};
	struct ec_response_fp_mode response;

	/* Setup authenticating state context. */
	global_context.fp_encryption_status |=
		FP_CONTEXT_STATUS_SESSION_ESTABLISHED;
	validate_request_return_val = EC_SUCCESS;

	/* Submit standard single-mode transition request. */
	int rv = ec_cmd_fp_mode_v1(NULL, &params, &response);

	/* Verify request is accepted with EC_RES_SUCCESS. */
	zassert_equal(rv, EC_RES_SUCCESS,
		      "Expected single auth mode request to succeed, got %d",
		      rv);

	/* Verify that the downstream mock was actually executed. */
	zassert_equal(validate_request_call_count, 1,
		      "Expected validate_request to be called exactly once");
}

ZTEST_USER(fpsensor_state, test_compound_fp_mode_auth_bypass_rejected)
{
	struct ec_params_fp_mode_v1 params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_MATCH,
		.mac = { 0xa5 },
	};
	struct ec_response_fp_mode response;

	global_context.fp_encryption_status |=
		FP_CONTEXT_STATUS_SESSION_ESTABLISHED;
	validate_request_return_val = EC_SUCCESS;

	int rv = ec_cmd_fp_mode_v1(NULL, &params, &response);

	zassert_equal(
		rv, EC_RES_ACCESS_DENIED,
		"Expected compound auth modes to return EC_RES_ACCESS_DENIED, got %d",
		rv);

	zassert_equal(
		validate_request_call_count, 0,
		"Expected validate_request to not be called due to early popcount rejection");
}

ZTEST_USER(fpsensor_state, test_two_step_bypass_mitigation)
{
	struct ec_params_fp_mode_v1 params = {};
	struct ec_response_fp_mode response;

	global_context.fp_encryption_status |=
		FP_CONTEXT_STATUS_SESSION_ESTABLISHED;

	/*
	 * STEP 1: Simulate the system already being in a non-auth-gated mode
	 * safely.
	 */
	global_context.sensor_mode = FP_MODE_DEEPSLEEP;

	/*
	 * STEP 2: Attacker attempts to leverage the current state delta to
	 * switch to an auth-gated mode (FP_MODE_MATCH). Because the production
	 * logic uses ec_cmd_fp_mode_v1, an empty payload is treated as a
	 * 32-byte zeroed array token. We instruct our mock cryptoprocessor to
	 * reject this invalid signature layout.
	 */
	params.mode = FP_MODE_DEEPSLEEP | FP_MODE_MATCH;
	validate_request_return_val = EC_ERROR_ACCESS_DENIED;

	int rv = ec_cmd_fp_mode_v1(NULL, &params, &response);

	/*
	 * Verify that because a token validation path was triggered by the
	 * state transition, the unauthorized token error bubbles up and
	 * correctly denies the command.
	 */
	zassert_equal(
		rv, EC_RES_ACCESS_DENIED,
		"Expected two-step state injection to return ACCESS_DENIED, got %d",
		rv);

	/*
	 * Verify that the mock validator was indeed targeted to inspect the
	 * token.
	 */
	zassert_equal(
		validate_request_call_count, 1,
		"Expected validate_request to be called to verify the state escalation step");
}

ZTEST_USER(fpsensor_state, test_validate_fp_mode_invalid_capture_type)
{
	struct ec_params_fp_mode_v1 params = {
		.mode = FP_MODE_CAPTURE |
			(FP_CAPTURE_TYPE_MAX << FP_MODE_CAPTURE_TYPE_SHIFT)
	};
	struct ec_response_fp_mode response;

	int rv = ec_cmd_fp_mode_v1(NULL, &params, &response);

	zassert_equal(
		rv, EC_RES_INVALID_PARAM,
		"Expected invalid capture type to return INVALID_PARAM, got %d",
		rv);

	zassert_equal(
		validate_request_call_count, 0,
		"Expected validate_request not to be called for invalid capture type");
}

ZTEST_USER(fpsensor_state, test_validate_fp_mode_invalid_algo_bits)
{
	struct ec_params_fp_mode_v1 params = {
		.mode = ~FP_VALID_MODES & ~FP_MODE_CAPTURE_TYPE_MASK,
	};
	struct ec_response_fp_mode response;

	int rv = ec_cmd_fp_mode_v1(NULL, &params, &response);

	zassert_equal(
		rv, EC_RES_INVALID_PARAM,
		"Expected invalid mode flags to return INVALID_PARAM, got %d",
		rv);

	zassert_equal(
		validate_request_call_count, 0,
		"Expected validate_request not to be called for invalid mode flags");
}

ZTEST_USER(fpsensor_state, test_validate_fp_mode_max_templates_exceeded)
{
	struct ec_params_fp_mode_v1 params = {
		.mode = FP_MODE_ENROLL_SESSION,
	};
	struct ec_response_fp_mode response;

	global_context.templ_valid = FP_MAX_FINGER_COUNT;

	int rv = ec_cmd_fp_mode_v1(NULL, &params, &response);

	zassert_equal(
		rv, EC_RES_INVALID_PARAM,
		"Expected ENROLL_SESSION with max templates to fail, got %d",
		rv);

	zassert_equal(
		validate_request_call_count, 0,
		"Expected validate_request not to be called when max templates exceeded");
}

ZTEST_USER(fpsensor_state,
	   test_validate_fp_mode_reset_sensor_while_active_fails)
{
	struct ec_params_fp_mode_v1 params = {
		.mode = FP_MODE_RESET_SENSOR,
	};
	struct ec_response_fp_mode response;

	global_context.sensor_mode = FP_MODE_MATCH;

	int rv = ec_cmd_fp_mode_v1(NULL, &params, &response);

	zassert_equal(
		rv, EC_RES_INVALID_PARAM,
		"Expected RESET_SENSOR while active to return INVALID_PARAM, got %d",
		rv);

	zassert_equal(
		validate_request_call_count, 0,
		"Expected validate_request not to be called when resetting active sensor");
}

ZTEST_USER(fpsensor_state,
	   test_validate_fp_mode_reset_sensor_while_idle_succeeds)
{
	struct ec_params_fp_mode_v1 params = {
		.mode = FP_MODE_RESET_SENSOR,
	};
	struct ec_response_fp_mode response;

	global_context.sensor_mode = 0;

	int rv = ec_cmd_fp_mode_v1(NULL, &params, &response);

	zassert_equal(rv, EC_RES_SUCCESS,
		      "Expected RESET_SENSOR while idle to succeed, got %d",
		      rv);

	zassert_equal(
		validate_request_call_count, 0,
		"Expected RESET_SENSOR while idle to bypass validate_request");
}

static void *fpsensor_setup(void)
{
	/* Start shimmed tasks. */
	start_ec_tasks();
	k_msleep(100);

	return NULL;
}

static void fpsensor_state_before(void *f)
{
	/* Reset context before each test case. */
	fp_reset_and_clear_context();

	/* Reset manual mock states. */
	validate_request_call_count = 0;
	validate_request_return_val = EC_SUCCESS;

	/* Reset remaining FFF mocks. */
	RESET_FAKE(mkbp_send_event);
}

ZTEST_SUITE(fpsensor_state, NULL, fpsensor_setup, fpsensor_state_before, NULL,
	    NULL);
