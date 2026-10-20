/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor.h>
#include <fpsensor/fpsensor_detect.h>
#include <fpsensor/fpsensor_utils.h>
#include <mkbp_event.h>
#include <rollback.h>
#include <string_view>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

ZTEST_SUITE(fpsensor_utils, nullptr, nullptr, nullptr, nullptr, nullptr);

ec_error_list rollback_get_secret(uint8_t *secret)
{
	// We should not call this function in the test.
	zassert_unreachable();

	return EC_ERROR_UNKNOWN;
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_success)
{
	zassert_equal(validate_fp_buffer_offset(1, 0, 1), EC_SUCCESS);
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_failure_no_overflow)
{
	zassert_equal(validate_fp_buffer_offset(1, 1, 1), EC_ERROR_INVAL);
}

ZTEST(fpsensor_utils, test_validate_fp_buffer_offset_failure_overflow)
{
	zassert_equal(validate_fp_buffer_offset(1, UINT32_MAX, 1),
		      EC_ERROR_OVERFLOW);
}

ZTEST(fpsensor_utils, test_is_finger_needed)
{
	zassert_true(is_finger_needed(
		FP_MODE_CAPTURE |
		(FP_CAPTURE_VENDOR_FORMAT << FP_MODE_CAPTURE_TYPE_SHIFT)));
	zassert_false(is_finger_needed(
		FP_MODE_CAPTURE |
		(FP_CAPTURE_DEFECT_PXL_TEST << FP_MODE_CAPTURE_TYPE_SHIFT)));
	zassert_false(is_finger_needed(
		FP_MODE_CAPTURE |
		(FP_CAPTURE_ABNORMAL_TEST << FP_MODE_CAPTURE_TYPE_SHIFT)));
	zassert_false(is_finger_needed(
		FP_MODE_CAPTURE |
		(FP_CAPTURE_NOISE_TEST << FP_MODE_CAPTURE_TYPE_SHIFT)));
	zassert_true(is_finger_needed(
		FP_MODE_CAPTURE |
		(FP_CAPTURE_SIMPLE_IMAGE << FP_MODE_CAPTURE_TYPE_SHIFT)));
	zassert_false(is_finger_needed(
		FP_MODE_CAPTURE |
		(FP_CAPTURE_PATTERN0 << FP_MODE_CAPTURE_TYPE_SHIFT)));
	zassert_false(is_finger_needed(
		FP_MODE_CAPTURE |
		(FP_CAPTURE_PATTERN1 << FP_MODE_CAPTURE_TYPE_SHIFT)));
	zassert_true(is_finger_needed(
		FP_MODE_CAPTURE |
		(FP_CAPTURE_QUALITY_TEST << FP_MODE_CAPTURE_TYPE_SHIFT)));
	zassert_false(is_finger_needed(
		FP_MODE_CAPTURE |
		(FP_CAPTURE_RESET_TEST << FP_MODE_CAPTURE_TYPE_SHIFT)));
	zassert_false(is_finger_needed(
		FP_MODE_CAPTURE |
		(FP_CAPTURE_PATTERN2 << FP_MODE_CAPTURE_TYPE_SHIFT)));

	/* Check the case where FP_MODE_CAPTURE is not set. */
	zassert_true(is_finger_needed(FP_CAPTURE_PATTERN0
				      << FP_MODE_CAPTURE_TYPE_SHIFT));
}

ZTEST(fpsensor_utils, test_skip_image_offset)
{
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
}

ZTEST(fpsensor_utils, test_fourcc_and_detect_strings)
{
	const auto expect_str = [](std::string_view actual,
				   std::string_view expected) {
		zassert_equal(actual, expected);
	};

	constexpr FpFourCc kGrey(FOURCC('G', 'R', 'E', 'Y'));
	static_assert(kGrey.view() == "GREY");
	expect_str(kGrey.c_str(), "GREY");
	expect_str(kGrey.view(), "GREY");
	expect_str(fourcc_to_string(FOURCC('G', 'R', 'E', 'Y')).c_str(),
		   "GREY");

	const FpFourCc non_printable(FOURCC('\x01', 'A', '\x7f', '~'));
	expect_str(non_printable.c_str(), ".A.~");

	expect_str(fp_transport_type_to_str(FP_TRANSPORT_TYPE_UNKNOWN),
		   "UNKNOWN");
	expect_str(fp_transport_type_to_str(FP_TRANSPORT_TYPE_SPI), "SPI");
	expect_str(fp_transport_type_to_str(FP_TRANSPORT_TYPE_UART), "UART");
	expect_str(fp_transport_type_to_str(FP_TRANSPORT_TYPE_USB), "USB");
	expect_str(fp_transport_type_to_str(static_cast<fp_transport_type>(99)),
		   "UNKNOWN");

	expect_str(fp_sensor_type_to_str(FP_SENSOR_TYPE_UNKNOWN), "UNKNOWN");
	expect_str(fp_sensor_type_to_str(FP_SENSOR_TYPE_FPC), "FPC");
	expect_str(fp_sensor_type_to_str(FP_SENSOR_TYPE_ELAN), "ELAN");
	expect_str(fp_sensor_type_to_str(FP_SENSOR_TYPE_EGIS), "EGIS");
	expect_str(fp_sensor_type_to_str(FP_SENSOR_TYPE_FOCALTECH),
		   "FOCALTECH");
	expect_str(fp_sensor_type_to_str(FP_SENSOR_TYPE_REALTEK), "REALTEK");
	expect_str(fp_sensor_type_to_str(static_cast<fp_sensor_type>(99)),
		   "UNKNOWN");

	expect_str(fp_sensor_spi_select_to_str(FP_SENSOR_SPI_SELECT_UNKNOWN),
		   "UNKNOWN");
	expect_str(
		fp_sensor_spi_select_to_str(FP_SENSOR_SPI_SELECT_DEVELOPMENT),
		"DEVELOPMENT");
	expect_str(fp_sensor_spi_select_to_str(FP_SENSOR_SPI_SELECT_PRODUCTION),
		   "PRODUCTION");
	expect_str(fp_sensor_spi_select_to_str(
			   static_cast<fp_sensor_spi_select>(99)),
		   "UNKNOWN");
}
