/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "fpsensor_test_utils.h"

#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <drivers/fingerprint_sim.h>
#include <fingerprint/v4l2_types.h>
#include <fpsensor/fpsensor_image_frame_params.h>
#include <memory>
#include <mkbp_event.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

#define IMAGE_SIZE                                                 \
	MAX_FROM_LIST(LISTIFY(NUM_IMAGE_CAPTURE_TYPES,             \
			      FINGERPRINT_SENSOR_FRAME_SIZE, (, ), \
			      DT_CHOSEN(cros_fp_fingerprint_sensor)))
static uint8_t __unused image_buffer[IMAGE_SIZE];

#define FP_SIMULATOR_IMAGE_FRAME_PARAM_INITIALIZER(idx, node_id)            \
	{                                                                   \
		.frame_size = FINGERPRINT_SENSOR_FRAME_SIZE(idx, node_id),  \
		.image_data_offset_bytes =                                  \
			FINGERPRINT_SENSOR_IMAGE_OFFSET(idx, node_id),      \
		.pixel_format =                                             \
			FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(idx, node_id), \
		.width = FINGERPRINT_SENSOR_RES_X(idx, node_id),            \
		.height = FINGERPRINT_SENSOR_RES_Y(idx, node_id),           \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(idx, node_id),            \
		.fp_capture_type =                                          \
			FINGERPRINT_SENSOR_CAPTURE_TYPE(idx, node_id),      \
		.reserved = 0,                                              \
	}

static const struct fingerprint_image_frame_params image_frame_params_arr[] = {
	LISTIFY(NUM_IMAGE_CAPTURE_TYPES,
		FP_SIMULATOR_IMAGE_FRAME_PARAM_INITIALIZER, (, ),
		DT_NODELABEL(fpsensor_sim))
};

static std::unique_ptr<FpImageFrameParamsCache> cache_fixture;

static uint32_t max_of(uint32_t fingerprint_image_frame_params::*field)
{
	uint32_t m = 0;

	for (const auto &p : image_frame_params_arr) {
		m = MAX(m, p.*field);
	}
	return m;
}

static void assert_cache_empty(const FpImageFrameParamsCache &cache)
{
	for (int i = 0; i < FP_CAPTURE_TYPE_MAX; ++i) {
		auto type = static_cast<enum fp_capture_type>(i);

		zassert_equal(0, cache.get_frame_size(type), "Type %d size", i);
		zassert_equal(0, cache.get_image_offset(type), "Type %d offset",
			      i);
	}
}

static void fpsensor_image_frame_params_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	const struct device *fp_sim =
		DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor));
	struct fingerprint_sensor_state state = {};

	fingerprint_get_state(fp_sim, &state);
	state.get_info_result = 0;
	fingerprint_set_state(fp_sim, &state);

	cache_fixture = std::make_unique<FpImageFrameParamsCache>();
	zassert_not_null(cache_fixture.get(),
			 "Memory allocation failed for cache.");

	cache_fixture->populate_cache(sizeof(image_buffer));
}

ZTEST_SUITE(fpsensor_image_frame_params, NULL, NULL,
	    fpsensor_image_frame_params_setup, NULL, NULL);

ZTEST(fpsensor_image_frame_params,
      test_cache_initialization_on_get_info_failure)
{
	const struct device *fp_sim =
		DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor));
	struct fingerprint_sensor_state state;

	fingerprint_get_state(fp_sim, &state);
	state.get_info_result = -EINVAL;
	fingerprint_set_state(fp_sim, &state);

	FpImageFrameParamsCache failing_cache{};
	zassert_true(FpImageFrameParamsCacheTestHelper::set_frame_size(
		failing_cache, FP_CAPTURE_VENDOR_FORMAT, 1024));

	const uint32_t max_configured_frame_size =
		max_of(&fingerprint_image_frame_params::frame_size);
	failing_cache.populate_cache(max_configured_frame_size);

	assert_cache_empty(failing_cache);
}

ZTEST(fpsensor_image_frame_params,
      test_cache_initialization_on_num_types_exceeded)
{
	FpImageFrameParamsCache failing_cache{};
	zassert_true(FpImageFrameParamsCacheTestHelper::set_frame_size(
		failing_cache, FP_CAPTURE_VENDOR_FORMAT, 1024));

	struct ec_response_fp_info_v3 info = {};
	info.sensor_info.num_capture_types = FP_CAPTURE_TYPE_MAX + 1;

	FpImageFrameParamsCacheTestHelper::populate_from_info(
		failing_cache, info, sizeof(image_buffer));

	assert_cache_empty(failing_cache);
}

ZTEST(fpsensor_image_frame_params,
      test_cache_initialization_on_invalid_capture_type)
{
	FpImageFrameParamsCache failing_cache{};
	zassert_true(FpImageFrameParamsCacheTestHelper::set_frame_size(
		failing_cache, FP_CAPTURE_VENDOR_FORMAT, 1024));

	struct {
		struct ec_response_fp_info_v3 info;
		struct fp_image_frame_params_v2 params[1];
	} packet = {};

	packet.info.sensor_info.num_capture_types = 1;
	packet.params[0].fp_capture_type = FP_CAPTURE_TYPE_MAX;
	packet.params[0].frame_size = 1024;
	packet.params[0].image_data_offset_bytes = 0;

	FpImageFrameParamsCacheTestHelper::populate_from_info(
		failing_cache, packet.info, sizeof(image_buffer));

	assert_cache_empty(failing_cache);
}

ZTEST(fpsensor_image_frame_params, test_cache_initialization_on_zero_frame_size)
{
	FpImageFrameParamsCache failing_cache{};
	zassert_true(FpImageFrameParamsCacheTestHelper::set_frame_size(
		failing_cache, FP_CAPTURE_VENDOR_FORMAT, 1024));

	struct {
		struct ec_response_fp_info_v3 info;
		struct fp_image_frame_params_v2 params[1];
	} packet = {};

	packet.info.sensor_info.num_capture_types = 1;
	packet.params[0].fp_capture_type = FP_CAPTURE_VENDOR_FORMAT;
	packet.params[0].frame_size = 0;
	packet.params[0].image_data_offset_bytes = 0;

	FpImageFrameParamsCacheTestHelper::populate_from_info(
		failing_cache, packet.info, sizeof(image_buffer));

	assert_cache_empty(failing_cache);
}

ZTEST(fpsensor_image_frame_params,
      test_cache_initialization_on_size_constraint_failure)
{
	zassert_true(ARRAY_SIZE(image_frame_params_arr) > 0,
		     "image_frame_params_arr must not be empty for this test.");

	const uint32_t max_configured_frame_size =
		max_of(&fingerprint_image_frame_params::frame_size);
	zassert_true(max_configured_frame_size > 0,
		     "Expected non-zero max configured frame size.");

	FpImageFrameParamsCache failing_cache{};
	zassert_true(FpImageFrameParamsCacheTestHelper::set_frame_size(
		failing_cache, FP_CAPTURE_VENDOR_FORMAT, 1024));
	failing_cache.populate_cache(max_configured_frame_size - 1);

	assert_cache_empty(failing_cache);
}

ZTEST(fpsensor_image_frame_params,
      test_cache_initialization_on_offset_constraint_failure)
{
	zassert_true(ARRAY_SIZE(image_frame_params_arr) > 0,
		     "image_frame_params_arr must not be empty for this test.");

	const uint32_t max_configured_offset = max_of(
		&fingerprint_image_frame_params::image_data_offset_bytes);
	zassert_true(max_configured_offset > 0,
		     "Expected non-zero max configured image offset.");

	FpImageFrameParamsCache failing_cache{};
	zassert_true(FpImageFrameParamsCacheTestHelper::set_frame_size(
		failing_cache, FP_CAPTURE_VENDOR_FORMAT, 1024));
	/*
	 * offset == max_image_buffer_bytes is rejected (leaves zero bytes for
	 * image data).
	 */
	failing_cache.populate_cache(max_configured_offset);

	assert_cache_empty(failing_cache);
}

ZTEST(fpsensor_image_frame_params,
      test_cache_valid_type_initialization_and_lookup)
{
	FpImageFrameParamsCache *cache = cache_fixture.get();

	for (const auto &params : image_frame_params_arr) {
		const uint32_t expected_size = params.frame_size;
		const uint32_t expected_offset = params.image_data_offset_bytes;
		const enum fp_capture_type type =
			static_cast<enum fp_capture_type>(
				params.fp_capture_type);

		zassert_true(
			type >= 0 && type < FP_CAPTURE_TYPE_MAX,
			"Configured type %d is out of expected range [0, %d).",
			type, FP_CAPTURE_TYPE_MAX);

		uint32_t actual_size = cache->get_frame_size(type);
		zassert_equal(
			expected_size, actual_size,
			"Size mismatch for configured type %d. Expected: %u, Actual: %u",
			static_cast<int>(type), expected_size, actual_size);

		uint32_t actual_offset = cache->get_image_offset(type);
		zassert_equal(
			expected_offset, actual_offset,
			"Offset mismatch for configured type %d. Expected: %u, Actual: %u",
			static_cast<int>(type), expected_offset, actual_offset);
	}
}

ZTEST(fpsensor_image_frame_params, test_cache_invalid_type_lookup)
{
	FpImageFrameParamsCache *cache = cache_fixture.get();

	std::array<bool, FP_CAPTURE_TYPE_MAX> is_type_present = {};
	for (const auto &params : image_frame_params_arr) {
		is_type_present[static_cast<enum fp_capture_type>(
			params.fp_capture_type)] = true;
	}

	for (int i = 0; i < FP_CAPTURE_TYPE_MAX; ++i) {
		enum fp_capture_type type =
			static_cast<enum fp_capture_type>(i);
		uint32_t actual_size = cache->get_frame_size(type);
		uint32_t actual_offset = cache->get_image_offset(type);

		if (is_type_present[i]) {
			zassert_true(
				actual_size > 0,
				"Configured Type %d should return non-zero size.",
				i);
		} else {
			zassert_equal(
				0, actual_size,
				"Unconfigured Type %d should return zero size.",
				i);
			zassert_equal(
				0, actual_offset,
				"Unconfigured Type %d should return zero offset.",
				i);
		}
	}
}

ZTEST(fpsensor_image_frame_params, test_cache_boundary_max_index)
{
	FpImageFrameParamsCache *cache = cache_fixture.get();

	zassert_equal(
		cache->get_frame_size(FP_CAPTURE_TYPE_MAX), 0,
		"Expected zero size for invalid capture type (FP_CAPTURE_TYPE_MAX).");
	zassert_equal(
		cache->get_image_offset(FP_CAPTURE_TYPE_MAX), 0,
		"Expected zero offset for invalid capture type (FP_CAPTURE_TYPE_MAX).");
}

ZTEST(fpsensor_image_frame_params, test_cache_boundary_above_max_index)
{
	FpImageFrameParamsCache *cache = cache_fixture.get();
	const enum fp_capture_type above_max_type =
		static_cast<enum fp_capture_type>(FP_CAPTURE_TYPE_MAX + 1);

	zassert_equal(
		cache->get_frame_size(above_max_type), 0,
		"Expected zero size for invalid capture type (FP_CAPTURE_TYPE_MAX + 1).");
	zassert_equal(
		cache->get_image_offset(above_max_type), 0,
		"Expected zero offset for invalid capture type (FP_CAPTURE_TYPE_MAX + 1).");
}

ZTEST(fpsensor_image_frame_params, test_cache_boundary_negative_index)
{
	FpImageFrameParamsCache *cache = cache_fixture.get();

	zassert_equal(
		cache->get_frame_size(static_cast<enum fp_capture_type>(-1)), 0,
		"Expected zero size for negative capture type (-1).");
	zassert_equal(
		cache->get_image_offset(static_cast<enum fp_capture_type>(-1)),
		0, "Expected zero offset for negative capture type (-1).");
}

ZTEST(fpsensor_image_frame_params, test_set_frame_size)
{
	FpImageFrameParamsCache *cache = cache_fixture.get();
	const enum fp_capture_type test_type = FP_CAPTURE_VENDOR_FORMAT;
	const uint32_t new_size = 1024;

	/* Verify the state before modification */
	uint32_t original_size = cache->get_frame_size(test_type);
	zassert_not_equal(
		new_size, original_size,
		"Test setup error: new_size should be different from original.");

	/* Use the test-only setter to override the cache */
	zassert_true(FpImageFrameParamsCacheTestHelper::set_frame_size(
			     *cache, test_type, new_size),
		     "Failed to set frame size for test type %d", test_type);

	/* Verify the state after modification */
	uint32_t updated_size = cache->get_frame_size(test_type);
	zassert_equal(
		updated_size, new_size,
		"Cache failed to update with set_frame_size. Expected: %u, Actual: %u",
		new_size, updated_size);
}

ZTEST(fpsensor_image_frame_params, test_set_image_offset)
{
	FpImageFrameParamsCache *cache = cache_fixture.get();
	const enum fp_capture_type test_type = FP_CAPTURE_VENDOR_FORMAT;
	const uint32_t new_offset = 64;

	FpImageFrameParam *param =
		FpImageFrameParamsCacheTestHelper::params(*cache, test_type);
	zassert_not_null(param, "Expected non-null params for test type %d",
			 test_type);
	param->image_data_offset_bytes = new_offset;

	uint32_t updated_offset = cache->get_image_offset(test_type);
	zassert_equal(
		updated_offset, new_offset,
		"Cache failed to update image offset. Expected: %u, Actual: %u",
		new_offset, updated_offset);
}

ZTEST(fpsensor_image_frame_params, test_set_frame_size_boundary_max_index)
{
	FpImageFrameParamsCache *cache = cache_fixture.get();
	const uint32_t new_size = 512;

	/* Verify that calling with FP_CAPTURE_TYPE_MAX is a no-op */
	zassert_false(FpImageFrameParamsCacheTestHelper::set_frame_size(
		*cache, FP_CAPTURE_TYPE_MAX, new_size));

	zassert_equal(
		cache->get_frame_size(FP_CAPTURE_TYPE_MAX), 0,
		"set_frame_size should ignore the out-of-bounds index FP_CAPTURE_TYPE_MAX.");
}

ZTEST(fpsensor_image_frame_params, test_set_frame_size_boundary_negative_index)
{
	FpImageFrameParamsCache *cache = cache_fixture.get();
	const uint32_t new_size = 512;

	/* Verify that calling with -1 is a no-op */
	zassert_false(FpImageFrameParamsCacheTestHelper::set_frame_size(
		*cache, static_cast<enum fp_capture_type>(-1), new_size));

	zassert_equal(
		cache->get_frame_size(static_cast<enum fp_capture_type>(-1)), 0,
		"set_frame_size should ignore the negative out-of-bounds index -1.");
}

ZTEST(fpsensor_image_frame_params, test_params_boundary_invalid_indices)
{
	FpImageFrameParamsCache *cache = cache_fixture.get();
	const enum fp_capture_type invalid_types[] = {
		FP_CAPTURE_TYPE_MAX,
		static_cast<enum fp_capture_type>(FP_CAPTURE_TYPE_MAX + 1),
		static_cast<enum fp_capture_type>(-1),
	};

	for (const auto type : invalid_types) {
		zassert_is_null(
			FpImageFrameParamsCacheTestHelper::params(*cache, type),
			"params() should return nullptr for out-of-bounds index %d.",
			static_cast<int>(type));
	}
}
