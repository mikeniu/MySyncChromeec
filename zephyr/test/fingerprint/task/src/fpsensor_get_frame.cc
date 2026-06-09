/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_test_utils.h"

#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor_frame_size.h>
#include <fpsensor/fpsensor_state.h>
#include <fpsensor/fpsensor_utils.h>
#include <system.h>

constexpr size_t kMaxReadSize = CONFIG_EC_HOST_CMD_HANDLER_TX_BUFFER_SIZE;

/* File-scoped reference buffer */
static uint8_t expected_canary[kMaxReadSize];

/* Forward declaration. */
enum ec_status get_frame(uint32_t offset, uint32_t size, uint8_t *output);

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

	/* Initialize the shared reference canary pattern once per test. */
	memset(expected_canary, 0xA5, sizeof(expected_canary));
}

ZTEST(fpsensor_get_frame, test_get_frame_system_locked)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint32_t size = kMaxReadSize;
	uint32_t offset = 0;

	system_is_locked_fake.return_val = 1;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_ACCESS_DENIED,
		      "Expected EC_RES_ACCESS_DENIED when system is locked");
	zassert_equal(system_is_locked_fake.call_count, 1,
		      "System lock state should be queried exactly once");

	/* Verify buffer integrity remains intact */
	zassert_mem_equal(output_buffer, expected_canary, sizeof(output_buffer),
			  "Output buffer modified during failure path");
}

ZTEST(fpsensor_get_frame, test_get_frame_invalid_capture_type)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint32_t size = kMaxReadSize;
	uint32_t offset = 0;

	global_context.current_capture_type = FP_CAPTURE_TYPE_INVALID;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_INVALID_PARAM,
		      "Expected EC_RES_INVALID_PARAM for invalid capture type");

	/* Verify buffer integrity remains intact */
	zassert_mem_equal(output_buffer, expected_canary, sizeof(output_buffer),
			  "Output buffer modified during failure path");
}

ZTEST(fpsensor_get_frame, test_get_frame_size_zero)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint32_t size = 0;
	uint32_t offset = 0;

	/* A size of 0 is a valid request that safely copies zero bytes.*/
	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_SUCCESS,
		      "Zero-length request should return EC_RES_SUCCESS");

	/* Verify buffer integrity remains intact */
	zassert_mem_equal(output_buffer, expected_canary, sizeof(output_buffer),
			  "Output buffer modified during failure path");
}

ZTEST(fpsensor_get_frame, test_get_frame_cache_size_exceeds_buffer)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint32_t size = kMaxReadSize;
	uint32_t offset = 0;

	/* Initialize the cache normally so it allocates its internal states. */
	global_context.fp_frame_size_cache.populate_cache(FP_SENSOR_IMAGE_SIZE);

	/*
	 * Cache Pollution: Use the test-only setter to override the cache value
	 * without violating strict aliasing rules.
	 */
	zassert_true(FpFrameSizeCacheTestHelper::set_frame_size(
			     global_context.fp_frame_size_cache,
			     FP_CAPTURE_VENDOR_FORMAT,
			     FP_SENSOR_IMAGE_SIZE + 1),
		     "Failed to set valid frame size for capture type %d",
		     FP_CAPTURE_VENDOR_FORMAT);

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_INVALID_PARAM,
		      "Expected cache size overflow failure");

	/* Verify buffer integrity remains intact */
	zassert_mem_equal(output_buffer, expected_canary, sizeof(output_buffer),
			  "Output buffer modified during failure path");
}

ZTEST(fpsensor_get_frame, test_get_frame_linear_overflow)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	/*
	 * Calculate the offset to force the final read position
	 * (offset + size) to equal exactly (FP_SENSOR_IMAGE_SIZE + 1).
	 */
	uint32_t size = kMaxReadSize;
	uint32_t offset = (FP_SENSOR_IMAGE_SIZE - size) + 1;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(
		status, EC_RES_INVALID_PARAM,
		"Expected EC_RES_INVALID_PARAM for tail-end linear buffer overflow");

	/* Verify buffer integrity remains intact */
	zassert_mem_equal(output_buffer, expected_canary, sizeof(output_buffer),
			  "Output buffer modified during failure path");
}

ZTEST(fpsensor_get_frame, test_get_frame_integer_overflow)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint32_t size = kMaxReadSize;
	uint32_t offset = UINT32_MAX;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(
		status, EC_RES_INVALID_PARAM,
		"Expected EC_RES_INVALID_PARAM for wrapped integer overflows");

	/* Verify buffer integrity remains intact */
	zassert_mem_equal(output_buffer, expected_canary, sizeof(output_buffer),
			  "Output buffer modified during failure path");
}

ZTEST(fpsensor_get_frame, test_get_frame_shifted_integer_overflow)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	global_context.current_capture_type = FP_CAPTURE_SIMPLE_IMAGE;

	uint32_t size = kMaxReadSize;
	uint32_t offset = UINT32_MAX - FP_SENSOR_IMAGE_OFFSET + 1;

	/*
	 * Verify that the offset + image_offset will definitely overflow 32
	 * bits
	 */
	zassert_true(
		(uint64_t)offset + FP_SENSOR_IMAGE_OFFSET > UINT32_MAX,
		"Test setup: Expected overflow condition, but calculation is safe.");

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(
		status, EC_RES_INVALID_PARAM,
		"Expected rejection of offset causing integer wrap-around");

	zassert_mem_equal(output_buffer, expected_canary, sizeof(output_buffer),
			  "Output buffer modified during overflow attack");
}

ZTEST(fpsensor_get_frame, test_get_frame_shifted_layout_success)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint8_t expected_output[kMaxReadSize];
	memset(expected_output, 0, sizeof(expected_output));

	uint32_t size = kMaxReadSize;
	uint32_t offset = 0;

	/* Set capture type to true for skip_image_offset() evaluation. */
	global_context.current_capture_type = FP_CAPTURE_SIMPLE_IMAGE;

	/*
	 * Simulate production buffer data layout: Shifting offset forward
	 * by the hardware-defined FP_SENSOR_IMAGE_OFFSET.
	 */
	uint32_t absolute_destination = FP_SENSOR_IMAGE_OFFSET + offset;

	zassert_true(
		absolute_destination + size <= FP_SENSOR_IMAGE_SIZE,
		"Test setup: Read range [%u, %u) exceeds fp_buffer size %u",
		absolute_destination, absolute_destination + size,
		FP_SENSOR_IMAGE_SIZE);

	fp_buffer[absolute_destination] = 0xDE;
	fp_buffer[absolute_destination + 1] = 0xAD;
	expected_output[0] = 0xDE;
	expected_output[1] = 0xAD;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_SUCCESS,
		      "Frame copy should succeed within shifted layout bounds");
	zassert_mem_equal(output_buffer, expected_output, sizeof(output_buffer),
			  "Full output buffer content mismatch");
}

ZTEST(fpsensor_get_frame, test_get_frame_cached_frame_boundary_success)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint8_t expected_output[kMaxReadSize];
	memset(expected_output, 0, sizeof(expected_output));

	uint32_t size = kMaxReadSize;

	global_context.current_capture_type = FP_CAPTURE_SIMPLE_IMAGE;

	/* Get the active real frame size limit dynamically from the cache. */
	uint32_t active_frame_size =
		global_context.fp_frame_size_cache.get_frame_size(
			FP_CAPTURE_SIMPLE_IMAGE);

	/*
	 * Align the final byte perfectly against the active image frame
	 * constraint limit.
	 */
	uint32_t offset = active_frame_size - size;

	uint32_t absolute_destination = FP_SENSOR_IMAGE_OFFSET + offset;

	zassert_true(
		absolute_destination + size <= FP_SENSOR_IMAGE_SIZE,
		"Test setup: Read range [%u, %u) exceeds fp_buffer size %u",
		absolute_destination, absolute_destination + size,
		FP_SENSOR_IMAGE_SIZE);

	fp_buffer[absolute_destination] = 0xBE;
	fp_buffer[absolute_destination + 1] = 0xEF;
	expected_output[0] = 0xBE;
	expected_output[1] = 0xEF;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(
		status, EC_RES_SUCCESS,
		"Frame copy should succeed at the exact maximum boundary ceiling");
	zassert_mem_equal(output_buffer, expected_output, sizeof(output_buffer),
			  "Full output buffer content mismatch");
}

ZTEST(fpsensor_get_frame, test_get_frame_cached_frame_boundary_overflow)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint32_t size = kMaxReadSize;

	global_context.current_capture_type = FP_CAPTURE_SIMPLE_IMAGE;

	uint32_t active_frame_size =
		global_context.fp_frame_size_cache.get_frame_size(
			FP_CAPTURE_SIMPLE_IMAGE);

	/*
	 * Push the chunk request exactly 1 byte past the active frame
	 * constraint boundary.
	 */
	uint32_t offset = (active_frame_size - size) + 1;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_INVALID_PARAM,
		      "Expected skipped offset overflow failure");

	/* Verify buffer integrity remains intact */
	zassert_mem_equal(output_buffer, expected_canary, sizeof(output_buffer),
			  "Output buffer modified during failure path");
}

ZTEST(fpsensor_get_frame, test_get_frame_physical_hardware_buffer_overflow)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint32_t size = kMaxReadSize;

	/*
	 * Land exactly 1 byte past the absolute array limit after the shift:
	 * (offset + FP_SENSOR_IMAGE_OFFSET + size) == sizeof(fp_buffer) + 1
	 */
	uint32_t offset =
		(sizeof(fp_buffer) + 1) - FP_SENSOR_IMAGE_OFFSET - size;

	/* Verify that the offset calculation did not underflow. */
	zassert_true(
		(sizeof(fp_buffer) + 1) >= (FP_SENSOR_IMAGE_OFFSET + size),
		"Test setup: Offset calculation wrapped. "
		"FP_SENSOR_IMAGE_OFFSET + size must be <= sizeof(fp_buffer) + 1.");

	global_context.current_capture_type = FP_CAPTURE_SIMPLE_IMAGE;

	/*
	 * Cache Pollution: Use the test-only setter to override the cache value
	 * without violating strict aliasing rules.
	 */
	zassert_true(FpFrameSizeCacheTestHelper::set_frame_size(
			     global_context.fp_frame_size_cache,
			     FP_CAPTURE_SIMPLE_IMAGE, sizeof(fp_buffer)),
		     "Failed to set valid frame size for capture type %d",
		     FP_CAPTURE_SIMPLE_IMAGE);

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_INVALID_PARAM,
		      "Expected skipped offset overflow failure");

	/* Verify buffer integrity remains intact */
	zassert_mem_equal(output_buffer, expected_canary, sizeof(output_buffer),
			  "Output buffer modified during failure path");
}

ZTEST(fpsensor_get_frame, test_get_frame_absolute_success)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint8_t expected_output[kMaxReadSize];
	memset(expected_output, 0, sizeof(expected_output));

	uint32_t size = kMaxReadSize;
	uint32_t offset = 0;

	fp_buffer[offset] = 0xAA;
	fp_buffer[offset + 1] = 0xBB;
	fp_buffer[offset + 2] = 0xCC;
	expected_output[0] = 0xAA;
	expected_output[1] = 0xBB;
	expected_output[2] = 0xCC;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(
		status, EC_RES_SUCCESS,
		"Standard clean requests should read directly from raw buffer ranges");
	zassert_mem_equal(output_buffer, expected_output, sizeof(output_buffer),
			  "Full output buffer content mismatch");
}

ZTEST_SUITE(fpsensor_get_frame, NULL, NULL, get_frame_before, NULL, NULL);
