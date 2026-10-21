/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor.h>
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
	zassert_equal(validate_fp_buffer_offset(1, 0, 1), EC_SUCCESS);
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_failure_no_overflow)
{
	static_assert(validate_fp_buffer_offset(1, 1, 1) == EC_ERROR_INVAL);
	zassert_equal(validate_fp_buffer_offset(1, 1, 1), EC_ERROR_INVAL);
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
					FP_CAPTURE_PATTERN0
						<< FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_true(is_finger_needed(FP_MODE_CAPTURE |
				      FP_CAPTURE_VENDOR_FORMAT
					      << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_finger_needed(FP_MODE_CAPTURE |
				       FP_CAPTURE_DEFECT_PXL_TEST
					       << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_finger_needed(FP_MODE_CAPTURE |
				       FP_CAPTURE_ABNORMAL_TEST
					       << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_finger_needed(FP_MODE_CAPTURE |
				       FP_CAPTURE_NOISE_TEST
					       << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_true(is_finger_needed(FP_MODE_CAPTURE |
				      FP_CAPTURE_SIMPLE_IMAGE
					      << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_finger_needed(FP_MODE_CAPTURE |
				       FP_CAPTURE_PATTERN0
					       << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_finger_needed(FP_MODE_CAPTURE |
				       FP_CAPTURE_PATTERN1
					       << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_true(is_finger_needed(FP_MODE_CAPTURE |
				      FP_CAPTURE_QUALITY_TEST
					      << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_finger_needed(FP_MODE_CAPTURE |
				       FP_CAPTURE_RESET_TEST
					       << FP_MODE_CAPTURE_TYPE_SHIFT));
	zassert_false(is_finger_needed(FP_MODE_CAPTURE |
				       FP_CAPTURE_PATTERN2
					       << FP_MODE_CAPTURE_TYPE_SHIFT));

	/* Check the case where FP_MODE_CAPTURE is not set. */
	zassert_true(is_finger_needed(FP_CAPTURE_PATTERN0
				      << FP_MODE_CAPTURE_TYPE_SHIFT));
}

ZTEST(fpsensor_utils, test_skip_image_offset)
{
	static_assert(!skip_image_offset(FP_CAPTURE_VENDOR_FORMAT));
	static_assert(skip_image_offset(FP_CAPTURE_SIMPLE_IMAGE));
	static_assert(!skip_image_offset(FP_CAPTURE_QUALITY_TEST));
	zassert_true(skip_image_offset(FP_CAPTURE_TYPE_INVALID));
	zassert_false(skip_image_offset(FP_CAPTURE_VENDOR_FORMAT));
	zassert_true(skip_image_offset(FP_CAPTURE_DEFECT_PXL_TEST));
	zassert_true(skip_image_offset(FP_CAPTURE_ABNORMAL_TEST));
	zassert_true(skip_image_offset(FP_CAPTURE_NOISE_TEST));
	zassert_true(skip_image_offset(FP_CAPTURE_SIMPLE_IMAGE));
	zassert_true(skip_image_offset(FP_CAPTURE_PATTERN0));
	zassert_true(skip_image_offset(FP_CAPTURE_PATTERN1));
	zassert_false(skip_image_offset(FP_CAPTURE_QUALITY_TEST));
	zassert_true(skip_image_offset(FP_CAPTURE_RESET_TEST));
	zassert_true(skip_image_offset(FP_CAPTURE_PATTERN2));
	zassert_true(skip_image_offset(FP_CAPTURE_TYPE_MAX));
}

ZTEST(fpsensor_utils, test_fp_match_success)
{
	static_assert(fp_match_success(EC_MKBP_FP_ERR_MATCH_YES));
	static_assert(fp_match_success(EC_MKBP_FP_ERR_MATCH_YES_UPDATED));
	static_assert(fp_match_success(EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED));
	static_assert(!fp_match_success(EC_MKBP_FP_ERR_MATCH_NO));
	static_assert(!fp_match_success(EC_MKBP_FP_ERR_MATCH_NO_INTERNAL));

	zassert_true(fp_match_success(EC_MKBP_FP_ERR_MATCH_YES));
	zassert_true(fp_match_success(EC_MKBP_FP_ERR_MATCH_YES_UPDATED));
	zassert_true(fp_match_success(EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED));
	zassert_false(fp_match_success(EC_MKBP_FP_ERR_MATCH_NO));
	zassert_false(fp_match_success(EC_MKBP_FP_ERR_MATCH_NO_INTERNAL));
}

ZTEST(fpsensor_utils, test_fourcc_constexpr_boundaries)
{
	constexpr FpFourCc kGrey(FOURCC('G', 'R', 'E', 'Y'));
	static_assert(kGrey.view() == "GREY");
	zassert_equal(std::string_view(kGrey.c_str()), "GREY");
	zassert_equal(fourcc_to_string(FOURCC('\x1f', ' ', '~', '\x7f')).view(),
		      ". ~.");
}
