/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor_modes.h>
#include <fpsensor/fpsensor_utils.h>
#include <mkbp_event.h>
#include <rollback.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

ZTEST_SUITE(fpsensor_utils, nullptr, nullptr, nullptr, nullptr, nullptr);

enum ec_error_list rollback_get_secret(uint8_t *secret)
{
	// We should not call this function in the test.
	zassert_unreachable();

	return EC_ERROR_UNKNOWN;
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_success)
{
	zassert_true(validate_fp_buffer_offset(1, 0, 1).has_value());
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_failure_no_overflow)
{
	zassert_equal(validate_fp_buffer_offset(1, 1, 1).error(),
		      EC_ERROR_INVAL);
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_failure_overflow)
{
	zassert_equal(validate_fp_buffer_offset(1, UINT32_MAX, 1).error(),
		      EC_ERROR_OVERFLOW);
}

ZTEST(fpsensor_utils, test_is_finger_needed)
{
	const auto check_mode = [](uint32_t mode_flags, fp_capture_type type) {
		const uint32_t raw_mode =
			mode_flags |
			FpCaptureType::create(type)->to_mode_bits();
		return FpSensorMode::create(raw_mode)->is_finger_needed();
	};

	zassert_true(check_mode(FP_MODE_CAPTURE, FP_CAPTURE_VENDOR_FORMAT));
	zassert_false(check_mode(FP_MODE_CAPTURE, FP_CAPTURE_DEFECT_PXL_TEST));
	zassert_false(check_mode(FP_MODE_CAPTURE, FP_CAPTURE_ABNORMAL_TEST));
	zassert_false(check_mode(FP_MODE_CAPTURE, FP_CAPTURE_NOISE_TEST));
	zassert_true(check_mode(FP_MODE_CAPTURE, FP_CAPTURE_SIMPLE_IMAGE));
	zassert_false(check_mode(FP_MODE_CAPTURE, FP_CAPTURE_PATTERN0));
	zassert_false(check_mode(FP_MODE_CAPTURE, FP_CAPTURE_PATTERN1));
	zassert_true(check_mode(FP_MODE_CAPTURE, FP_CAPTURE_QUALITY_TEST));
	zassert_false(check_mode(FP_MODE_CAPTURE, FP_CAPTURE_RESET_TEST));
	zassert_false(check_mode(FP_MODE_CAPTURE, FP_CAPTURE_PATTERN2));

	/* Check the case where FP_MODE_CAPTURE is not set. */
	zassert_true(check_mode(0, FP_CAPTURE_PATTERN0));
}

ZTEST(fpsensor_utils, test_skip_image_offset)
{
	const auto check_type = [](fp_capture_type type) {
		return FpCaptureType::create(type)->skip_image_offset();
	};

	zassert_false(check_type(FP_CAPTURE_VENDOR_FORMAT));
	zassert_true(check_type(FP_CAPTURE_DEFECT_PXL_TEST));
	zassert_true(check_type(FP_CAPTURE_ABNORMAL_TEST));
	zassert_true(check_type(FP_CAPTURE_NOISE_TEST));
	zassert_true(check_type(FP_CAPTURE_SIMPLE_IMAGE));
	zassert_true(check_type(FP_CAPTURE_PATTERN0));
	zassert_true(check_type(FP_CAPTURE_PATTERN1));
	zassert_false(check_type(FP_CAPTURE_QUALITY_TEST));
	zassert_true(check_type(FP_CAPTURE_RESET_TEST));
	zassert_true(check_type(FP_CAPTURE_PATTERN2));
}

ZTEST(fpsensor_utils, test_fp_mkbp_event_and_v0_packing)
{
	const FpMkbpEvent enroll_evt{ make_fp_enroll_event(
		EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY, 42) };
	zassert_true(enroll_evt.is_enroll());
	zassert_false(enroll_evt.is_match());
	zassert_equal(enroll_evt.error_code(),
		      EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY);
	zassert_equal(enroll_evt.enroll_progress(), 42);

	const FpMkbpEvent match_evt{ make_fp_match_event(
		EC_MKBP_FP_ERR_MATCH_YES, 3) };
	zassert_true(match_evt.is_match());
	zassert_false(match_evt.is_enroll());
	zassert_equal(match_evt.error_code(), EC_MKBP_FP_ERR_MATCH_YES);
	zassert_equal(match_evt.match_index(), 3);

	const FpMkbpEvent status_evt{ EC_MKBP_FP_FINGER_DOWN |
				      EC_MKBP_FP_FINGER_UP |
				      EC_MKBP_FP_IMAGE_READY };
	zassert_true(status_evt.is_finger_down());
	zassert_true(status_evt.is_finger_up());
	zassert_true(status_evt.is_image_ready());

	constexpr FpFrameV0Offset frame_offset{
		.buffer_index = FP_FRAME_INDEX_TEMPLATE + 2,
		.offset = 128,
	};
	constexpr auto unpacked_offset =
		FpFrameV0Offset::unpack(frame_offset.pack());
	zassert_false(unpacked_offset.is_raw_image());
	zassert_equal(unpacked_offset.template_index(), 2);
	zassert_equal(unpacked_offset.offset, 128U);

	constexpr FpTemplateV0Size templ_size{
		.size = 256,
		.commit = true,
	};
	constexpr auto unpacked_size =
		FpTemplateV0Size::unpack(templ_size.pack());
	zassert_equal(unpacked_size.size, 256U);
	zassert_true(unpacked_size.commit);
}
