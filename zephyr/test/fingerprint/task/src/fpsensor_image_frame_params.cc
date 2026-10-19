/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "fpsensor_test_utils.h"

#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <drivers/fingerprint_sim.h>
#include <fpsensor/fpsensor_image_frame_params.h>
#include <mkbp_event.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

namespace
{

FpImageFrameParamsCache cache_fixture;

void fpsensor_image_frame_params_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	cache_fixture = FpImageFrameParamsCache{};
	cache_fixture.populate_cache(IMAGE_SIZE);
}

} /* namespace */

ZTEST_SUITE(fpsensor_image_frame_params, nullptr, nullptr,
	    fpsensor_image_frame_params_setup, nullptr, nullptr);

ZTEST(fpsensor_image_frame_params,
      test_cache_initialization_on_size_constraint_failure)
{
	zassert_true(
		!kDefaultSimImageFrameParams.empty(),
		"kDefaultSimImageFrameParams must not be empty for this test.");

	uint32_t max_frame_size_bytes =
		kDefaultSimImageFrameParams[0].frame_size - 1;

	FpImageFrameParamsCache failing_cache{};
	failing_cache.populate_cache(max_frame_size_bytes);

	for (int i = 0; i < FP_CAPTURE_TYPE_MAX; ++i) {
		fp_capture_type type = static_cast<fp_capture_type>(i);
		auto actual_size = failing_cache.get_frame_size(type);

		zassert_false(actual_size.has_value(),
			      "Unconfigured Type %d should return an error.",
			      i);
		zassert_equal(actual_size.error(), EC_ERROR_INVAL);
	}
}

ZTEST(fpsensor_image_frame_params,
      test_cache_valid_type_initialization_and_lookup)
{
	for (const auto &params : kDefaultSimImageFrameParams) {
		const uint32_t expected_size = params.frame_size;
		const fp_capture_type type =
			static_cast<fp_capture_type>(params.fp_capture_type);

		zassert_true(
			type >= 0 && type < FP_CAPTURE_TYPE_MAX,
			"Configured type %d is out of expected range [0, %d).",
			type, FP_CAPTURE_TYPE_MAX);

		auto actual_size = cache_fixture.get_frame_size(type);
		zassert_true(actual_size.has_value());
		zassert_equal(
			expected_size, *actual_size,
			"Mismatch for configured type %d. Expected: %u, Actual: %u",
			static_cast<int>(type), expected_size, *actual_size);
	}
}

ZTEST(fpsensor_image_frame_params, test_cache_invalid_type_lookup)
{
	for (int i = 0; i < FP_CAPTURE_TYPE_MAX; ++i) {
		fp_capture_type type = static_cast<fp_capture_type>(i);
		auto actual_size = cache_fixture.get_frame_size(type);
		const bool is_type_present = std::ranges::contains(
			kDefaultSimImageFrameParams, type,
			&fingerprint_image_frame_params::fp_capture_type);

		if (is_type_present) {
			zassert_true(
				actual_size.has_value() && *actual_size > 0,
				"Configured Type %d should return non-zero size.",
				i);
		} else {
			zassert_false(
				actual_size.has_value(),
				"Unconfigured Type %d should return an error.",
				i);
			zassert_equal(actual_size.error(), EC_ERROR_INVAL);
		}
	}
}

ZTEST(fpsensor_image_frame_params, test_cache_boundary_max_index)
{
	zassert_equal(
		cache_fixture.get_frame_size(FP_CAPTURE_TYPE_MAX).error(),
		EC_ERROR_INVAL,
		"Expected EC_ERROR_INVAL for invalid capture type (FP_CAPTURE_TYPE_MAX).");
}

ZTEST(fpsensor_image_frame_params, test_cache_boundary_above_max_index)
{
	const fp_capture_type above_max_type =
		static_cast<fp_capture_type>(FP_CAPTURE_TYPE_MAX + 1);

	zassert_equal(
		cache_fixture.get_frame_size(above_max_type).error(),
		EC_ERROR_INVAL,
		"Expected EC_ERROR_INVAL for invalid capture type (FP_CAPTURE_TYPE_MAX + 1).");
}

ZTEST(fpsensor_image_frame_params, test_cache_boundary_negative_index)
{
	zassert_equal(
		cache_fixture.get_frame_size(static_cast<fp_capture_type>(-1))
			.error(),
		EC_ERROR_INVAL,
		"Expected EC_ERROR_INVAL for negative capture type (-1).");
}

ZTEST(fpsensor_image_frame_params, test_set_frame_size)
{
	const fp_capture_type test_type = FP_CAPTURE_VENDOR_FORMAT;
	const uint32_t new_size = 1024;

	/* Verify the state before modification */
	uint32_t original_size = *cache_fixture.get_frame_size(test_type);
	zassert_not_equal(
		new_size, original_size,
		"Test setup error: new_size should be different from original.");

	/* Use the test-only setter to override the cache */
	zassert_true(FpImageFrameParamsCacheTestHelper::set_frame_size(
			     cache_fixture, test_type, new_size),
		     "Failed to set frame size for test type %d", test_type);

	/* Verify the state after modification */
	uint32_t updated_size = *cache_fixture.get_frame_size(test_type);
	zassert_equal(
		updated_size, new_size,
		"Cache failed to update with set_frame_size. Expected: %u, Actual: %u",
		new_size, updated_size);
}

ZTEST(fpsensor_image_frame_params, test_set_frame_size_boundary_max_index)
{
	const uint32_t new_size = 512;

	/* Verify that calling with FP_CAPTURE_TYPE_MAX is a no-op */
	zassert_false(FpImageFrameParamsCacheTestHelper::set_frame_size(
		cache_fixture, FP_CAPTURE_TYPE_MAX, new_size));

	zassert_equal(
		cache_fixture.get_frame_size(FP_CAPTURE_TYPE_MAX).error(),
		EC_ERROR_INVAL,
		"set_frame_size should ignore the out-of-bounds index FP_CAPTURE_TYPE_MAX.");
}

ZTEST(fpsensor_image_frame_params, test_set_frame_size_boundary_negative_index)
{
	const uint32_t new_size = 512;

	/* Verify that calling with -1 is a no-op */
	zassert_false(FpImageFrameParamsCacheTestHelper::set_frame_size(
		cache_fixture, static_cast<fp_capture_type>(-1), new_size));

	zassert_equal(
		cache_fixture.get_frame_size(static_cast<fp_capture_type>(-1))
			.error(),
		EC_ERROR_INVAL,
		"set_frame_size should ignore the negative out-of-bounds index -1.");
}
