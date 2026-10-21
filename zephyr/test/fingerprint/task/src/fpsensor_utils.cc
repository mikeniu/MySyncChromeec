/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_test_utils.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor.h>
#include <fpsensor/fpsensor_frame_storage.h>
#include <fpsensor/fpsensor_utils.h>
#include <mkbp_event.h>
#include <rollback.h>
#include <string_view>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

ZTEST_SUITE(fpsensor_utils, NULL, NULL, NULL, NULL, NULL);

enum ec_error_list rollback_get_secret(uint8_t *secret)
{
	// We should not call this function in the test.
	zassert_unreachable();

	return EC_ERROR_UNKNOWN;
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_success)
{
	static_assert(validate_fp_buffer_offset(1, 0, 1) == EC_SUCCESS);
	static_assert(validate_fp_buffer_offset(2, 0, 1) == EC_SUCCESS);
	zassert_equal(validate_fp_buffer_offset(1, 0, 1), EC_SUCCESS);
	zassert_equal(validate_fp_buffer_offset(2, 0, 1), EC_SUCCESS);
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_failure_no_overflow)
{
	static_assert(validate_fp_buffer_offset(1, 1, 1) == EC_ERROR_INVAL);
	static_assert(validate_fp_buffer_offset(1, UINT32_MAX - 1, 1) ==
		      EC_ERROR_INVAL);
	zassert_equal(validate_fp_buffer_offset(1, 1, 1), EC_ERROR_INVAL);
	zassert_equal(validate_fp_buffer_offset(1, UINT32_MAX - 1, 1),
		      EC_ERROR_INVAL);
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_failure_overflow)
{
	static_assert(validate_fp_buffer_offset(1, UINT32_MAX, 1) ==
		      EC_ERROR_OVERFLOW);
	zassert_equal(validate_fp_buffer_offset(1, UINT32_MAX, 1),
		      EC_ERROR_OVERFLOW);
}

ZTEST(fpsensor_utils, test_is_finger_needed)
{
	static_assert(is_finger_needed(FP_MODE_CAPTURE |
				       FP_CAPTURE_VENDOR_FORMAT
					       << FP_MODE_CAPTURE_TYPE_SHIFT));
	static_assert(!is_finger_needed(FP_MODE_CAPTURE |
					FP_CAPTURE_DEFECT_PXL_TEST
						<< FP_MODE_CAPTURE_TYPE_SHIFT));
	static_assert(!is_finger_needed(FP_MODE_CAPTURE |
					FP_CAPTURE_ABNORMAL_TEST
						<< FP_MODE_CAPTURE_TYPE_SHIFT));
	static_assert(!is_finger_needed(FP_MODE_CAPTURE |
					FP_CAPTURE_NOISE_TEST
						<< FP_MODE_CAPTURE_TYPE_SHIFT));
	static_assert(is_finger_needed(FP_MODE_CAPTURE |
				       FP_CAPTURE_SIMPLE_IMAGE
					       << FP_MODE_CAPTURE_TYPE_SHIFT));
	static_assert(!is_finger_needed(FP_MODE_CAPTURE |
					FP_CAPTURE_PATTERN0
						<< FP_MODE_CAPTURE_TYPE_SHIFT));
	static_assert(!is_finger_needed(FP_MODE_CAPTURE |
					FP_CAPTURE_PATTERN1
						<< FP_MODE_CAPTURE_TYPE_SHIFT));
	static_assert(is_finger_needed(FP_MODE_CAPTURE |
				       FP_CAPTURE_QUALITY_TEST
					       << FP_MODE_CAPTURE_TYPE_SHIFT));
	static_assert(!is_finger_needed(FP_MODE_CAPTURE |
					FP_CAPTURE_RESET_TEST
						<< FP_MODE_CAPTURE_TYPE_SHIFT));
	static_assert(!is_finger_needed(FP_MODE_CAPTURE |
					FP_CAPTURE_PATTERN2
						<< FP_MODE_CAPTURE_TYPE_SHIFT));

	/* Check the case where FP_MODE_CAPTURE is not set. */
	static_assert(is_finger_needed(FP_CAPTURE_PATTERN0
				       << FP_MODE_CAPTURE_TYPE_SHIFT));
}

ZTEST(fpsensor_utils, test_skip_image_offset)
{
	static_assert(skip_image_offset(FP_CAPTURE_TYPE_INVALID));
	static_assert(!skip_image_offset(FP_CAPTURE_VENDOR_FORMAT));
	static_assert(skip_image_offset(FP_CAPTURE_DEFECT_PXL_TEST));
	static_assert(skip_image_offset(FP_CAPTURE_ABNORMAL_TEST));
	static_assert(skip_image_offset(FP_CAPTURE_NOISE_TEST));
	static_assert(skip_image_offset(FP_CAPTURE_SIMPLE_IMAGE));
	static_assert(skip_image_offset(FP_CAPTURE_PATTERN0));
	static_assert(skip_image_offset(FP_CAPTURE_PATTERN1));
	static_assert(!skip_image_offset(FP_CAPTURE_QUALITY_TEST));
	static_assert(skip_image_offset(FP_CAPTURE_RESET_TEST));
	static_assert(skip_image_offset(FP_CAPTURE_PATTERN2));
	static_assert(skip_image_offset(FP_CAPTURE_TYPE_MAX));
}

ZTEST(fpsensor_utils, test_fp_match_success)
{
	static_assert(fp_match_success(EC_MKBP_FP_ERR_MATCH_YES));
	static_assert(fp_match_success(EC_MKBP_FP_ERR_MATCH_YES_UPDATED));
	static_assert(fp_match_success(EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED));

	static_assert(!fp_match_success(EC_MKBP_FP_ERR_MATCH_NO));
	static_assert(!fp_match_success(EC_MKBP_FP_ERR_MATCH_NO_INTERNAL));
	static_assert(!fp_match_success(EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES));
	static_assert(!fp_match_success(EC_MKBP_FP_ERR_MATCH_NO_AUTH_FAIL));
	static_assert(!fp_match_success(EC_MKBP_FP_ERR_MATCH_NO_LOW_QUALITY));
	static_assert(!fp_match_success(EC_MKBP_FP_ERR_MATCH_NO_LOW_COVERAGE));
}

ZTEST(fpsensor_utils, test_fourcc_constexpr_boundaries)
{
	constexpr FpFourCc kGrey(FOURCC('G', 'R', 'E', 'Y'));
	static_assert(kGrey.view() == "GREY");
	zassert_equal(std::string_view(kGrey.c_str()), "GREY");
	zassert_equal(fourcc_to_string(FOURCC('\x1f', ' ', '~', '\x7f')).view(),
		      ". ~.");
}

ZTEST(fpsensor_utils, test_mode_and_capture_type_wrappers)
{
	zassert_false(FpCaptureType::create(-1).has_value());
	zassert_false(FpCaptureType::create(FP_CAPTURE_TYPE_MAX).has_value());
	zassert_true(FpCaptureType::create(0).has_value());
	zassert_true(
		FpCaptureType::create(FP_CAPTURE_TYPE_MAX - 1).has_value());

	zassert_false(FpSensorMode::create(FP_MODE_ENROLL_IMAGE).has_value());
	zassert_false(FpSensorMode::create(0x00100000U).has_value());
	zassert_equal(FpSensorMode::assume_valid(FP_MODE_MATCH).raw(),
		      FP_MODE_MATCH);
	const auto enroll_mode = FpSensorMode::create(FP_MODE_ENROLL_SESSION |
						      FP_MODE_ENROLL_IMAGE);
	zassert_true(enroll_mode.has_value());
	zassert_true(enroll_mode->is_enroll_session());
	zassert_true(enroll_mode->is_detect_finger());
	zassert_true(enroll_mode->is_finger_needed());
	zassert_equal(enroll_mode->raw(),
		      FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE);

	const auto pattern_mode = FpSensorMode::create(
		FP_MODE_CAPTURE |
		FpCaptureType::create(FP_CAPTURE_PATTERN0)->to_mode_bits());
	zassert_true(pattern_mode.has_value());
	zassert_false(pattern_mode->is_finger_needed());
}

ZTEST(fpsensor_utils, test_checked_subspan_boundaries)
{
	std::array<uint8_t, 4> buf{ 10, 20, 30, 40 };
	const auto sub = checked_subspan(std::span(buf), 1, 3);
	zassert_true(sub.has_value());
	zassert_equal(sub->size(), 3u);
	zassert_equal((*sub)[0], 20);
	zassert_equal(checked_subspan(std::span(buf), 2, 3).error(),
		      EC_ERROR_INVAL);
	zassert_equal(checked_subspan(std::span(buf), UINT32_MAX, 1).error(),
		      EC_ERROR_OVERFLOW);
}

ZTEST(fpsensor_utils, test_fp_frame_storage_slicing)
{
	std::array<uint8_t, FP_SENSOR_IMAGE_OFFSET + 8> buf{};
	for (size_t i = 0; i < buf.size(); ++i) {
		buf[i] = static_cast<uint8_t>(i + 1);
	}
	FpImageFrameParamsCache cache{};
	std::array<uint8_t, 2> out{};

	zassert_equal(fp_frame_storage::captured_frame(buf, FP_CAPTURE_TYPE_MAX,
						       cache)
			      .error(),
		      EC_RES_INVALID_PARAM);
	zassert_equal(fp_frame_storage::captured_frame(
			      buf, FP_CAPTURE_VENDOR_FORMAT, cache)
			      .error(),
		      EC_RES_INVALID_PARAM);

	FpImageFrameParamsCacheTestHelper::set_frame_size(
		cache, FP_CAPTURE_VENDOR_FORMAT, buf.size() + 1);
	zassert_equal(fp_frame_storage::captured_frame(
			      buf, FP_CAPTURE_VENDOR_FORMAT, cache)
			      .error(),
		      EC_RES_INVALID_PARAM);

	FpImageFrameParamsCacheTestHelper::set_frame_size(
		cache, FP_CAPTURE_VENDOR_FORMAT, 4);
	zassert_true(fp_frame_storage::get_raw_frame(
			     buf, FP_CAPTURE_VENDOR_FORMAT, cache, 2, out)
			     .has_value());
	zassert_equal(out[0], buf[2]);
	zassert_equal(out[1], buf[3]);
	zassert_equal(fp_frame_storage::get_raw_frame(
			      buf, FP_CAPTURE_VENDOR_FORMAT, cache, 3, out)
			      .error(),
		      EC_RES_INVALID_PARAM);

	FpImageFrameParamsCacheTestHelper::set_frame_size(
		cache, FP_CAPTURE_SIMPLE_IMAGE, 4);
	const auto simple_slice = fp_frame_storage::captured_frame(
		buf, FP_CAPTURE_SIMPLE_IMAGE, cache);
	zassert_true(simple_slice.has_value());
	zassert_equal((*simple_slice)[0], buf[FP_SENSOR_IMAGE_OFFSET]);
}

ZTEST(fpsensor_utils, test_rate_limiter_boundaries)
{
	FpRateLimiter limiter{};
	const FpTimestamp t0{ std::chrono::seconds(10) };

	zassert_equal(limiter.try_acquire(t0), EC_HOST_CMD_SUCCESS);

	zassert_equal(limiter.try_acquire(t0 + std::chrono::seconds(1) -
					  std::chrono::microseconds(1)),
		      EC_HOST_CMD_BUSY);

	zassert_equal(limiter.try_acquire(t0 + std::chrono::seconds(1)),
		      EC_HOST_CMD_SUCCESS);

	zassert_equal(limiter.try_acquire(t0 + std::chrono::seconds(2) +
					  std::chrono::microseconds(1)),
		      EC_HOST_CMD_SUCCESS);
}
