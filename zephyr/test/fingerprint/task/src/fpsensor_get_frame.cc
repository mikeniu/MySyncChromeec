/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor_frame_size.h>
#include <fpsensor/fpsensor_state.h>
#include <fpsensor/fpsensor_utils.h>
#include <system.h>

enum ec_status get_frame(uint32_t offset, uint32_t size, uint8_t *output);

extern uint8_t fp_buffer[FP_SENSOR_IMAGE_SIZE];
extern struct fpsensor_context global_context;

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, system_is_locked);
FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

/**
 * @brief Environment setup executed before every individual test run.
 */
static void get_frame_before(void *f)
{
	/* Reset histories and clear call counts for all FFF fakes. */
	RESET_FAKE(system_is_locked);
	RESET_FAKE(mkbp_send_event);

	/* Set up baseline pass configurations. */
	system_is_locked_fake.return_val = 0;
	mkbp_send_event_fake.return_val = 0;

	/* Populate the C++ class cache object layout matrix. */
	global_context.fp_frame_size_cache.populate_cache(sizeof(fp_buffer));
	global_context.current_capture_type = FP_CAPTURE_VENDOR_FORMAT;

	/* Zero-out the shared frame buffer. */
	memset(fp_buffer, 0, FP_SENSOR_IMAGE_SIZE);
}

ZTEST(fpsensor_get_frame, test_get_frame_system_locked)
{
	uint8_t output_buffer[16];
	uint32_t offset = 0;
	uint32_t size = sizeof(output_buffer);

	system_is_locked_fake.return_val = 1;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_ACCESS_DENIED,
		      "Expected EC_RES_ACCESS_DENIED when system is locked");
	zassert_equal(system_is_locked_fake.call_count, 1,
		      "System lock state should be queried exactly once");
}

ZTEST(fpsensor_get_frame, test_get_frame_invalid_capture_type)
{
	uint8_t output_buffer[16];
	uint32_t offset = 0;
	uint32_t size = sizeof(output_buffer);

	global_context.current_capture_type = FP_CAPTURE_TYPE_INVALID;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_INVALID_PARAM,
		      "Expected EC_RES_INVALID_PARAM for invalid capture type");
}

ZTEST(fpsensor_get_frame, test_get_frame_cache_size_exceeds_buffer)
{
	uint8_t output_buffer[16];
	uint32_t offset = 0;
	uint32_t size = sizeof(output_buffer);

	/*
	 * First, initialize the cache normally so it allocates its internal
	 * tracking states.
	 */
	global_context.fp_frame_size_cache.populate_cache(FP_SENSOR_IMAGE_SIZE);
	global_context.current_capture_type = FP_CAPTURE_VENDOR_FORMAT;

	/*
	 * Cache Pollution: Bypass the strict populate_cache constraint
	 * checks by finding the memory location of the array slot and writing
	 * an oversized value directly into it.
	 */
	uint32_t *internal_array_ptr =
		(uint32_t *)&global_context.fp_frame_size_cache;

	/*
	 * Corrupt the slot matching target capture type
	 * FP_CAPTURE_VENDOR_FORMAT.
	 */
	internal_array_ptr[FP_CAPTURE_VENDOR_FORMAT] =
		FP_SENSOR_IMAGE_SIZE + 100;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_INVALID_PARAM,
		      "Expected cache size overflow failure");
}

ZTEST(fpsensor_get_frame, test_get_frame_linear_overflow)
{
	uint8_t output_buffer[16];
	uint32_t offset = FP_SENSOR_IMAGE_SIZE - 5;
	uint32_t size = 10;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(
		status, EC_RES_INVALID_PARAM,
		"Expected EC_RES_INVALID_PARAM for tail-end linear buffer overflow");
}

ZTEST(fpsensor_get_frame, test_get_frame_integer_overflow)
{
	uint8_t output_buffer[16];
	uint32_t offset = UINT32_MAX;
	uint32_t size = 10;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(
		status, EC_RES_INVALID_PARAM,
		"Expected EC_RES_INVALID_PARAM for wrapped integer overflows");
}

ZTEST(fpsensor_get_frame, test_get_frame_skip_offset_success)
{
	uint8_t output_buffer[4];
	uint32_t offset = 5;
	uint32_t size = sizeof(output_buffer);

	/*
	 * FP_CAPTURE_SIMPLE_IMAGE automatically maps to true inside compiled
	 * skip_image_offset().
	 */
	global_context.current_capture_type = FP_CAPTURE_SIMPLE_IMAGE;

	/*
	 * Inject mock tracking data exactly at the expected internal image
	 * shift destination.
	 */
	uint32_t absolute_destination = FP_SENSOR_IMAGE_OFFSET + offset;

	fp_buffer[absolute_destination] = 0xDE;
	fp_buffer[absolute_destination + 1] = 0xAD;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_SUCCESS,
		      "Frame copy should succeed within shifted layout bounds");
	zassert_equal(output_buffer[0], 0xDE,
		      "Byte mismatch at output index 0");
	zassert_equal(output_buffer[1], 0xAD,
		      "Byte mismatch at output index 1");
}

ZTEST(fpsensor_get_frame, test_get_frame_skip_offset_out_of_bounds)
{
	uint8_t output_buffer[10];
	uint32_t offset = FP_SENSOR_IMAGE_SIZE - 5;
	uint32_t size = 2;

	global_context.current_capture_type = FP_CAPTURE_SIMPLE_IMAGE;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_INVALID_PARAM,
		      "Expected skipped offset overflow failure");
}

ZTEST(fpsensor_get_frame, test_get_frame_absolute_success)
{
	uint8_t output_buffer[3];
	uint32_t offset = 50;
	uint32_t size = sizeof(output_buffer);

	/*
	 * FP_CAPTURE_VENDOR_FORMAT evaluates to false inside
	 * skip_image_offset().
	 */
	global_context.current_capture_type = FP_CAPTURE_VENDOR_FORMAT;

	fp_buffer[offset] = 0xAA;
	fp_buffer[offset + 1] = 0xBB;
	fp_buffer[offset + 2] = 0xCC;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(
		status, EC_RES_SUCCESS,
		"Standard clean requests should read directly from raw buffer ranges");
	zassert_equal(output_buffer[0], 0xAA,
		      "Data verification failed at index 0");
	zassert_equal(output_buffer[1], 0xBB,
		      "Data verification failed at index 1");
	zassert_equal(output_buffer[2], 0xCC,
		      "Data verification failed at index 2");
}

ZTEST_SUITE(fpsensor_get_frame, NULL, NULL, get_frame_before, NULL, NULL);