/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_test_utils.h"
#include "mock_fingerprint_algorithm.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <ec_commands.h>
#include <ec_tasks.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

namespace
{

FpSensorInfoBuffer test_info_buffer;

void verify_maintenance_dead_pixels(int reported_bad_pixels,
				    int expected_dead_pixels)
{
	fingerprint_sensor_state state;

	update_sim_state([reported_bad_pixels](auto &s) {
		s.bad_pixels = reported_bad_pixels;
	});

	/* Change fingerprint mode to maintenance. */
	zassert_true(set_fp_mode(FP_MODE_SENSOR_MAINTENANCE) &
		     FP_MODE_SENSOR_MAINTENANCE);

	/* Check that maintenance was run. */
	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.maintenance_ran);

	/* Confirm that number of dead pixels is correct. */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(FP_ERROR_DEAD_PIXELS(
			      test_info_buffer.header.sensor_info.errors),
		      expected_dead_pixels);

	/*
	 * Confirm that maintenance flag is not set after the maintenance
	 * operation is finished.
	 */
	zassert_false(get_fp_mode() & FP_MODE_SENSOR_MAINTENANCE);
}

void *fpsensor_setup()
{
	/* Start shimmed tasks. */
	start_ec_tasks();
	k_msleep(100);

	return nullptr;
}

void fpsensor_before(void *f)
{
	zassert_equal(set_fp_mode(0), 0);

	reset_sim_state();
	RESET_FAKE(mkbp_send_event);
}

} /* namespace */

ZTEST_USER(fpsensor_init, test_tpm_seed_init)
{
	ec_response_fp_encryption_status status;
	ec_params_fp_seed params = {
		.struct_version = 4,
		.reserved = 0,
		.seed = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
			  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			  0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F },
	};

	/* Get FP encryption flags. */
	zassert_ok(ec_cmd_fp_encryption_status(nullptr, &status));

	/* Confirm TPM seed is not set */
	zassert_true(status.valid_flags & FP_ENC_STATUS_SEED_SET);
	zassert_false(status.status & FP_ENC_STATUS_SEED_SET);

	/* Set TPM seed. */
	zassert_ok(ec_cmd_fp_seed(nullptr, &params));

	/* Get FP encryption flags. */
	zassert_ok(ec_cmd_fp_encryption_status(nullptr, &status));

	/* Confirm that FP_ENC_STATUS_SEED_SET is set. */
	zassert_true(status.valid_flags & FP_ENC_STATUS_SEED_SET);
	zassert_true(status.status & FP_ENC_STATUS_SEED_SET);

	/* Try to set TPM seed once again (should fail). */
	zassert_equal(EC_RES_ACCESS_DENIED, ec_cmd_fp_seed(nullptr, &params));
}

ZTEST_USER(fpsensor_init, test_tpm_seed_invalid)
{
	ec_params_fp_seed params = {
		/* 0 is not a valid structure version. */
		.struct_version = 0,
		.reserved = 0,
		.seed = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
			  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			  0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F },
	};

	/* Try to set TPM seed (should fail). */
	zassert_equal(EC_RES_INVALID_PARAM, ec_cmd_fp_seed(nullptr, &params));
}

ZTEST_USER(fpsensor_init, test_set_fp_context)
{
	ec_params_fp_context_v1 params = {
		.action = FP_CONTEXT_ASYNC,
		.userid = { 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8 },
	};
	ec_response_fp_encryption_status status;

	/* Set context (asynchronously). */
	zassert_ok(ec_cmd_fp_context_v1(nullptr, &params));

	/* Now any attempt to set context should return EC_RES_BUSY. */
	zassert_equal(EC_RES_BUSY, ec_cmd_fp_context_v1(nullptr, &params));

	/* Now any attempt to get command result should return EC_RES_BUSY. */
	params.action = FP_CONTEXT_GET_RESULT;
	zassert_equal(EC_RES_BUSY, ec_cmd_fp_context_v1(nullptr, &params));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Get command result. */
	zassert_ok(ec_cmd_fp_context_v1(nullptr, &params));

	/* Get FP encryption flags. */
	zassert_ok(ec_cmd_fp_encryption_status(nullptr, &status));

	/* Confirm that FP_CONTEXT_USER_ID_SET is set. */
	zassert_true(status.status & FP_CONTEXT_USER_ID_SET);
}

ZTEST_USER(fpsensor_init, test_maintenance_mode_dead_pixel_3)
{
	/* Confirm that number of dead pixels is unknown initially. */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(FP_ERROR_DEAD_PIXELS(
			      test_info_buffer.header.sensor_info.errors),
		      FP_ERROR_DEAD_PIXELS_UNKNOWN);

	verify_maintenance_dead_pixels(3, 3);
}

ZTEST_USER(fpsensor_init, test_maintenance_mode_dead_pixel_max_plus_2)
{
	verify_maintenance_dead_pixels(FP_ERROR_DEAD_PIXELS_MAX + 2,
				       FP_ERROR_DEAD_PIXELS_MAX);
}

ZTEST_USER(fpsensor_init, test_maintenance_mode_dead_pixel_max)
{
	verify_maintenance_dead_pixels(FP_ERROR_DEAD_PIXELS_MAX,
				       FP_ERROR_DEAD_PIXELS_MAX);
}

ZTEST_USER(fpsensor_init, test_maintenance_mode_dead_pixel_max_minus_1)
{
	verify_maintenance_dead_pixels(FP_ERROR_DEAD_PIXELS_MAX - 1,
				       FP_ERROR_DEAD_PIXELS_MAX - 1);
}

ZTEST_USER(fpsensor_init, test_fp_info_v3_overflow)
{
	zassert_equal(EC_RES_OVERFLOW,
		      get_fp_info_v3(test_info_buffer,
				     sizeof(ec_response_fp_info_v3) - 1));
}

ZTEST_USER(fpsensor_init, test_fp_info_v3_get_info_error)
{
	update_sim_state([](auto &state) { state.get_info_result = -EINVAL; });

	zassert_equal(EC_RES_UNAVAILABLE, get_fp_info_v3(test_info_buffer));
}

ZTEST_SUITE(fpsensor_init, nullptr, fpsensor_setup, fpsensor_before, nullptr,
	    nullptr);
