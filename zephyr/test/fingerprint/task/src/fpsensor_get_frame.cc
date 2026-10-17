/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_test_utils.h"

#include <zephyr/autoconf.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor_image_frame_params.h>
#include <fpsensor/fpsensor_state.h>
#include <fpsensor/fpsensor_utils.h>
#include <system.h>

constexpr size_t kMaxReadSize = CONFIG_EC_HOST_CMD_HANDLER_TX_BUFFER_SIZE;
constexpr uint8_t kCanaryByte = 0xA5;

/* File-scoped output buffer */
static std::array<uint8_t, kMaxReadSize> output_buffer;

/* Forward declaration. */
std::expected<void, ec_status> get_frame(uint32_t offset,
					 std::span<uint8_t> output);

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
	global_context.frame.frame_params_cache.populate_cache(
		sizeof(fp_buffer));
	global_context.frame.current_capture_type =
		*FpCaptureType::create(FP_CAPTURE_VENDOR_FORMAT);

	/* Zero-out the shared frame buffer. */
	std::ranges::fill(fp_buffer, 0);

	/* Initialize the shared output canary pattern once per test. */
	output_buffer.fill(kCanaryByte);
}

static void expect_get_frame_unchanged(uint32_t offset, uint32_t size,
				       enum ec_status expected_status)
{
	const auto res =
		get_frame(offset, std::span(output_buffer).first(size));
	zassert_equal(res.error_or(EC_RES_SUCCESS), expected_status);
	zassert_true(
		std::ranges::all_of(output_buffer,
				    [](uint8_t b) { return b == kCanaryByte; }),
		"Output buffer modified during failure/no-op path");
}

ZTEST(fpsensor_get_frame, test_get_frame_system_locked)
{
	system_is_locked_fake.return_val = 1;

	expect_get_frame_unchanged(0, kMaxReadSize, EC_RES_ACCESS_DENIED);
	zassert_equal(system_is_locked_fake.call_count, 1,
		      "System lock state should be queried exactly once");
}

ZTEST(fpsensor_get_frame, test_get_frame_invalid_capture_type)
{
	global_context.frame.current_capture_type = std::nullopt;

	expect_get_frame_unchanged(0, kMaxReadSize, EC_RES_INVALID_PARAM);
}

ZTEST(fpsensor_get_frame, test_get_frame_size_zero)
{
	/* A size of 0 is a valid request that safely copies zero bytes. */
	expect_get_frame_unchanged(0, 0, EC_RES_SUCCESS);
}

ZTEST(fpsensor_get_frame, test_get_frame_cache_size_exceeds_buffer)
{
	/* Initialize the cache normally so it allocates its internal states. */
	global_context.frame.frame_params_cache.populate_cache(
		FP_SENSOR_IMAGE_SIZE);

	/*
	 * Cache Pollution: Use the test-only setter to override the cache value
	 * without violating strict aliasing rules.
	 */
	zassert_true(FpImageFrameParamsCacheTestHelper::set_frame_size(
			     global_context.frame.frame_params_cache,
			     FP_CAPTURE_VENDOR_FORMAT,
			     FP_SENSOR_IMAGE_SIZE + 1),
		     "Failed to set valid frame size for capture type %d",
		     FP_CAPTURE_VENDOR_FORMAT);

	expect_get_frame_unchanged(0, kMaxReadSize, EC_RES_INVALID_PARAM);
}

ZTEST(fpsensor_get_frame, test_get_frame_linear_overflow)
{
	/*
	 * Calculate the offset to force the final read position
	 * (offset + size) to equal exactly (FP_SENSOR_IMAGE_SIZE + 1).
	 */
	constexpr uint32_t offset = (FP_SENSOR_IMAGE_SIZE - kMaxReadSize) + 1;

	expect_get_frame_unchanged(offset, kMaxReadSize, EC_RES_INVALID_PARAM);
}

ZTEST(fpsensor_get_frame, test_get_frame_integer_overflow)
{
	expect_get_frame_unchanged(UINT32_MAX, kMaxReadSize,
				   EC_RES_INVALID_PARAM);
}

ZTEST(fpsensor_get_frame, test_get_frame_shifted_integer_overflow)
{
	global_context.frame.current_capture_type =
		*FpCaptureType::create(FP_CAPTURE_SIMPLE_IMAGE);

	constexpr uint32_t offset = UINT32_MAX - FP_SENSOR_IMAGE_OFFSET + 1;

	/*
	 * Verify that the offset + image_offset will definitely overflow 32
	 * bits.
	 */
	static_assert(static_cast<uint64_t>(offset) + FP_SENSOR_IMAGE_OFFSET >
		      UINT32_MAX);

	expect_get_frame_unchanged(offset, kMaxReadSize, EC_RES_INVALID_PARAM);
}

ZTEST(fpsensor_get_frame, test_get_frame_shifted_layout_success)
{
	std::array<uint8_t, kMaxReadSize> expected_output{};

	uint32_t size = kMaxReadSize;
	uint32_t offset = 0;

	/* Set capture type to true for skip_image_offset() evaluation. */
	global_context.frame.current_capture_type =
		*FpCaptureType::create(FP_CAPTURE_SIMPLE_IMAGE);

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

	zassert_true(get_frame(offset, std::span(output_buffer).first(size))
			     .has_value(),
		     "Frame copy should succeed within shifted layout bounds");
	zassert_true(output_buffer == expected_output,
		     "Full output buffer content mismatch");
}

ZTEST(fpsensor_get_frame, test_get_frame_cached_frame_boundary_success)
{
	std::array<uint8_t, kMaxReadSize> expected_output{};

	uint32_t size = kMaxReadSize;

	global_context.frame.current_capture_type =
		*FpCaptureType::create(FP_CAPTURE_SIMPLE_IMAGE);

	/* Get the active real frame size limit dynamically from the cache. */
	uint32_t active_frame_size =
		*global_context.frame.frame_params_cache.get_frame_size(
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

	zassert_true(
		get_frame(offset, std::span(output_buffer).first(size))
			.has_value(),
		"Frame copy should succeed at the exact maximum boundary ceiling");
	zassert_true(output_buffer == expected_output,
		     "Full output buffer content mismatch");
}

ZTEST(fpsensor_get_frame, test_get_frame_cached_frame_boundary_overflow)
{
	global_context.frame.current_capture_type =
		*FpCaptureType::create(FP_CAPTURE_SIMPLE_IMAGE);

	uint32_t active_frame_size =
		*global_context.frame.frame_params_cache.get_frame_size(
			FP_CAPTURE_SIMPLE_IMAGE);

	/*
	 * Push the chunk request exactly 1 byte past the active frame
	 * constraint boundary.
	 */
	uint32_t offset = (active_frame_size - kMaxReadSize) + 1;

	expect_get_frame_unchanged(offset, kMaxReadSize, EC_RES_INVALID_PARAM);
}

ZTEST(fpsensor_get_frame, test_get_frame_physical_hardware_buffer_overflow)
{
	/*
	 * Land exactly 1 byte past the absolute array limit after the shift:
	 * (offset + FP_SENSOR_IMAGE_OFFSET + size) == sizeof(fp_buffer) + 1
	 */
	constexpr uint32_t offset =
		(sizeof(fp_buffer) + 1) - FP_SENSOR_IMAGE_OFFSET - kMaxReadSize;

	/* Verify that the offset calculation did not underflow. */
	static_assert((sizeof(fp_buffer) + 1) >=
		      (FP_SENSOR_IMAGE_OFFSET + kMaxReadSize));

	global_context.frame.current_capture_type =
		*FpCaptureType::create(FP_CAPTURE_SIMPLE_IMAGE);

	/*
	 * Cache Pollution: Use the test-only setter to override the cache value
	 * without violating strict aliasing rules.
	 */
	zassert_true(FpImageFrameParamsCacheTestHelper::set_frame_size(
			     global_context.frame.frame_params_cache,
			     FP_CAPTURE_SIMPLE_IMAGE, sizeof(fp_buffer)),
		     "Failed to set valid frame size for capture type %d",
		     FP_CAPTURE_SIMPLE_IMAGE);

	expect_get_frame_unchanged(offset, kMaxReadSize, EC_RES_INVALID_PARAM);
}

ZTEST(fpsensor_get_frame, test_get_frame_absolute_success)
{
	std::array<uint8_t, kMaxReadSize> expected_output{};

	uint32_t size = kMaxReadSize;
	uint32_t offset = 0;

	fp_buffer[offset] = 0xAA;
	fp_buffer[offset + 1] = 0xBB;
	fp_buffer[offset + 2] = 0xCC;
	expected_output[0] = 0xAA;
	expected_output[1] = 0xBB;
	expected_output[2] = 0xCC;

	zassert_true(
		get_frame(offset, std::span(output_buffer).first(size))
			.has_value(),
		"Standard clean requests should read directly from raw buffer ranges");
	zassert_true(output_buffer == expected_output,
		     "Full output buffer content mismatch");
}

ZTEST_SUITE(fpsensor_get_frame, nullptr, nullptr, get_frame_before, nullptr,
	    nullptr);
