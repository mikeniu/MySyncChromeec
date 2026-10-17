/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "console.h"
#include "fpsensor_test_utils.h"
#include "system.h"

#include <stdio.h>

#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <array>
#include <ec_commands.h>
#include <expected>
#include <fpsensor/fpsensor_modes.h>
#include <fpsensor/fpsensor_state_driver.h>
#include <fpsensor/fpsensor_utils.h>
#include <fpsensor_driver.h>
#include <mkbp_event.h>
#include <optional>
#include <rollback.h>

static_assert(sizeof(struct fp_image_frame_params_v2) ==
		      sizeof(struct fingerprint_image_frame_params),
	      "Frame param structures must be the same size");

std::expected<fp_image_frame_params_v2, ec_error_list>
get_image_frame_params(std::optional<FpCaptureType> capture_type);

static int is_locked;

int system_is_locked(void)
{
	return is_locked;
}

static int run_console_cmd(const char *cmd, bool locked = false)
{
	is_locked = locked ? 1 : 0;
	return shell_execute_cmd(get_ec_shell(), cmd);
}

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

ZTEST_SUITE(fpsensor_debug, nullptr, nullptr, nullptr, nullptr, nullptr);

ZTEST(fpsensor_debug, test_console_fpinfo)
{
	zassert_equal(run_console_cmd("fpinfo"), EC_SUCCESS);
}

/* TODO(b/371647536): Add other tests of commands in fpsensor_debug to verify
 * entire handlers.
 */
ZTEST(fpsensor_debug, test_command_fpupload_success)
{
	zassert_equal(run_console_cmd("fpupload 52 image"), EC_SUCCESS);
}

ZTEST(fpsensor_debug, test_command_fpupload_system_is_locked)
{
	zassert_equal(run_console_cmd("fpupload 52 image", /*locked=*/true),
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST(fpsensor_debug, test_command_fpupload_one_argument)
{
	zassert_equal(run_console_cmd("fpupload 52"), EC_ERROR_PARAM_COUNT);
}

ZTEST(fpsensor_debug, test_command_fpupload_three_arguments)
{
	zassert_equal(run_console_cmd("fpupload 52 image 78"),
		      EC_ERROR_PARAM_COUNT);
}

ZTEST(fpsensor_debug, test_command_fpupload_negative_offset)
{
	zassert_equal(run_console_cmd("fpupload -1 image"), EC_ERROR_PARAM1);
}

ZTEST(fpsensor_debug,
      test_command_fpupload_offset_equal_image_size_minus_image_offset)

{
	char console_input[] = "fpupload " STRINGIFY(UINT32_MAX) " image";
	snprintf(console_input, sizeof(console_input),
		 "fpupload %" PRIu32 " image",
		 FP_SENSOR_IMAGE_SIZE - FP_SENSOR_IMAGE_OFFSET);
	zassert_equal(run_console_cmd(console_input), EC_ERROR_PARAM1);
}

ZTEST(fpsensor_debug, test_command_fpupload_correct_uploaded_values)
{
	std::ranges::fill(fp_buffer, 0);
	zassert_equal(run_console_cmd("fpupload 0 f16e38"), EC_SUCCESS);
	zassert_equal(fp_buffer[FP_SENSOR_IMAGE_OFFSET], 241);
	zassert_equal(fp_buffer[FP_SENSOR_IMAGE_OFFSET + 1], 110);
	zassert_equal(fp_buffer[FP_SENSOR_IMAGE_OFFSET + 2], 56);
}

/* TODO(b/371647536): Add other tests of commands in fpsensor_debug to verify
 * entire handlers.
 */
ZTEST(fpsensor_debug, test_command_fpcapture_system_is_locked)
{
	zassert_equal(run_console_cmd("fpcapture", /*locked=*/true),
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST(fpsensor_debug, test_command_fpcapture_mode_is_negative)
{
	zassert_equal(run_console_cmd("fpcapture -1"), EC_ERROR_PARAM1);
}

ZTEST(fpsensor_debug, test_command_fpcapture_mode_is_too_large)
{
	char console_input[32];
	snprintf(console_input, sizeof(console_input), "fpcapture %d",
		 FP_CAPTURE_TYPE_MAX);
	zassert_equal(run_console_cmd(console_input), EC_ERROR_PARAM1);
}

/* TODO(b/371647536): Add other tests of commands in fpsensor_debug to verify
 * entire handlers.
 */
ZTEST(fpsensor_debug, test_command_fpenroll)
{
	zassert_equal(run_console_cmd("fpenroll", /*locked=*/true),
		      EC_ERROR_ACCESS_DENIED);
}

std::expected<void, ec_error_list>
upload_pgm_image(std::span<const uint8_t> frame,
		 const struct fp_image_frame_params_v2 &image_frame_params);

ZTEST(fpsensor_debug, test_upload_pgm_image_wrong_bpp)
{
	std::array<uint8_t, 100> frame{};

	zassert_equal(upload_pgm_image(frame, { .bpp = 0 }).error(),
		      EC_ERROR_UNKNOWN);

	zassert_equal(upload_pgm_image(frame, { .bpp = 17 }).error(),
		      EC_ERROR_UNKNOWN);

	zassert_equal(upload_pgm_image(frame, { .bpp = 23 }).error(),
		      EC_ERROR_UNKNOWN);
}

ZTEST(fpsensor_debug, test_get_image_frame_params)
{
	constexpr auto kCaptureTypesArray = std::to_array(
		{ FP_CAPTURE_VENDOR_FORMAT, FP_CAPTURE_DEFECT_PXL_TEST,
		  FP_CAPTURE_ABNORMAL_TEST, FP_CAPTURE_NOISE_TEST,
		  FP_CAPTURE_SIMPLE_IMAGE, FP_CAPTURE_PATTERN0,
		  FP_CAPTURE_PATTERN1, FP_CAPTURE_QUALITY_TEST,
		  FP_CAPTURE_RESET_TEST, FP_CAPTURE_PATTERN2,
		  FP_CAPTURE_TYPE_MAX });

	for (enum fp_capture_type current_capture_type : kCaptureTypesArray) {
		const auto it = std::ranges::find(
			kDefaultSimImageFrameParams, current_capture_type,
			&fingerprint_image_frame_params::fp_capture_type);
		const struct fingerprint_image_frame_params *expected_params =
			(it != kDefaultSimImageFrameParams.end()) ? &(*it) :
								    nullptr;

		const auto capture_type =
			FpCaptureType::create(current_capture_type);
		const auto result = get_image_frame_params(
			capture_type ? std::optional(*capture_type) :
				       std::nullopt);

		if (expected_params) {
			zassert_true(
				result.has_value(),
				"get_image_frame_params failed for capture type %d",
				current_capture_type);
			zassert_mem_equal(
				&(*result), expected_params,
				sizeof(struct fp_image_frame_params_v2),
				"Struct comparison failed for type %d",
				current_capture_type);
		} else {
			zassert_false(result.has_value());
			zassert_equal(
				result.error(), EC_ERROR_INVAL,
				"Expected EC_ERROR_INVAL (%d), but got %d for invalid type",
				EC_ERROR_INVAL, result.error());
		}
	}
}
