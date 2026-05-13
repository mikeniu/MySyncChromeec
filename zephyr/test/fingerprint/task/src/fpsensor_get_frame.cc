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

/* Standard host data payload chunk size. */
constexpr int kMaxReadSize = 536;

/* File-scoped reference buffer */
static uint8_t expected_canary[kMaxReadSize];

/* Forward declaration. */
enum ec_status get_frame(uint32_t offset, uint32_t size, uint8_t *output);

/* Global firmware dependencies linked into the test binary */
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

	/* Initialize the shared reference canary pattern once per test. */
	memset(expected_canary, 0xA5, sizeof(expected_canary));
}

ZTEST(fpsensor_get_frame, test_get_frame_system_locked)
{
	uint8_t output_buffer[kMaxReadSize];
	memset(output_buffer, 0xA5, sizeof(output_buffer));

	uint32_t size = kMaxReadSize;
	uint32_t offset = 3 * kMaxReadSize;

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
	uint32_t offset = 3 * kMaxReadSize;

	global_context.current_capture_type = FP_CAPTURE_TYPE_INVALID;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(status, EC_RES_INVALID_PARAM,
		      "Expected EC_RES_INVALID_PARAM for invalid capture type");

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
	 * Cache Pollution: Access the underlying std::array container directly.
	 * A std::array has the same alignment and layout as its internal raw
	 * storage array, allowing a standard, type-safe overlay adjustment
	 * without strict aliasing violations.
	 */
	auto *raw_storage =
		reinterpret_cast<std::array<uint32_t, FP_CAPTURE_TYPE_MAX> *>(
			&global_context.fp_frame_size_cache);
	(*raw_storage)[FP_CAPTURE_VENDOR_FORMAT] = FP_SENSOR_IMAGE_SIZE + 1;

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

ZTEST(fpsensor_get_frame, test_get_frame_shifted_layout_success)
{
	uint8_t output_buffer[kMaxReadSize];
	uint32_t size = kMaxReadSize;
	/* Calculate offset dynamically based on 3rd chunk index. */
	uint32_t offset = 3 * kMaxReadSize;

	/* Set capture type to true for skip_image_offset() evaluation. */
	global_context.current_capture_type = FP_CAPTURE_SIMPLE_IMAGE;

	/*
	 * Simulate production buffer data layout: Shifting offset forward
	 * by the hardware-defined FP_SENSOR_IMAGE_OFFSET.
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

ZTEST(fpsensor_get_frame, test_get_frame_cached_frame_boundary_success)
{
	uint8_t output_buffer[kMaxReadSize];
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
	fp_buffer[absolute_destination] = 0xBE;
	fp_buffer[absolute_destination + 1] = 0xEF;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(
		status, EC_RES_SUCCESS,
		"Frame copy should succeed at the exact maximum boundary ceiling");
	zassert_equal(output_buffer[0], 0xBE,
		      "Byte mismatch at output index 0");
	zassert_equal(output_buffer[1], 0xEF,
		      "Byte mismatch at output index 1");
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

	global_context.current_capture_type = FP_CAPTURE_SIMPLE_IMAGE;

	/*
	 * Cache Pollution: Temporarily override the cache for SIMPLE_IMAGE to
	 * return the absolute maximum size. This allows the large offset to
	 * pass the first validator, dropping execution right into the target
	 * check.
	 */
	auto *raw_storage =
		reinterpret_cast<std::array<uint32_t, FP_CAPTURE_TYPE_MAX> *>(
			&global_context.fp_frame_size_cache);
	(*raw_storage)[FP_CAPTURE_SIMPLE_IMAGE] = sizeof(fp_buffer);

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
	uint32_t size = kMaxReadSize;
	/* Calculate offset dynamically based on 3rd chunk index. */
	uint32_t offset = 3 * kMaxReadSize;

	fp_buffer[offset] = 0xAA;
	fp_buffer[offset + 1] = 0xBB;
	fp_buffer[offset + 2] = 0xCC;

	enum ec_status status = get_frame(offset, size, output_buffer);

	zassert_equal(
		status, EC_RES_SUCCESS,
		"Standard clean requests should read directly from raw buffer ranges");
	zassert_equal(output_buffer[0], 0xAA, "Data mismatch at index 0");
	zassert_equal(output_buffer[1], 0xBB, "Data mismatch at index 1");
	zassert_equal(output_buffer[2], 0xCC, "Data mismatch at index 2");
}

ZTEST_SUITE(fpsensor_get_frame, NULL, NULL, get_frame_before, NULL, NULL);
