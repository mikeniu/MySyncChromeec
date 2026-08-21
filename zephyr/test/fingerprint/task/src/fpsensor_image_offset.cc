/* Copyright 2026 The ChromiumOS Authors
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
#include <fpsensor/fpsensor_image_offset.h>
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

static std::unique_ptr<FpImageOffsetCache> cache_fixture;

static void fpsensor_image_offset_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	cache_fixture = std::make_unique<FpImageOffsetCache>();
	zassert_not_null(cache_fixture.get(),
			 "Memory allocation failed for cache.");

	cache_fixture->populate_cache(sizeof(image_buffer));
}

ZTEST_SUITE(fpsensor_image_offset, NULL, NULL, fpsensor_image_offset_setup,
	    NULL, NULL);

ZTEST(fpsensor_image_offset,
      test_cache_initialization_on_offset_constraint_failure)
{
	zassert_true(ARRAY_SIZE(image_frame_params_arr) > 0,
		     "image_frame_params_arr must not be empty for this test.");

	/* Find maximum configured offset */
	uint32_t max_configured_offset = 0;
	for (const auto &params : image_frame_params_arr) {
		max_configured_offset = MAX(max_configured_offset,
					    params.image_data_offset_bytes);
	}

	if (max_configured_offset > 0) {
		FpImageOffsetCache failing_cache{};
		failing_cache.populate_cache(max_configured_offset - 1);

		for (int i = 0; i < FP_CAPTURE_TYPE_MAX; ++i) {
			enum fp_capture_type type =
				static_cast<enum fp_capture_type>(i);
			uint32_t actual_offset =
				failing_cache.get_image_offset(type);

			zassert_equal(
				0, actual_offset,
				"Type %d should return zero offset after validation failure.",
				i);
		}
	}
}

ZTEST(fpsensor_image_offset, test_cache_valid_type_initialization_and_lookup)
{
	FpImageOffsetCache *cache = cache_fixture.get();

	for (const auto &params : image_frame_params_arr) {
		const uint32_t expected_offset = params.image_data_offset_bytes;
		const enum fp_capture_type type =
			static_cast<enum fp_capture_type>(
				params.fp_capture_type);

		zassert_true(
			type >= 0 && type < FP_CAPTURE_TYPE_MAX,
			"Configured type %d is out of expected range [0, %d).",
			type, FP_CAPTURE_TYPE_MAX);

		uint32_t actual_offset = cache->get_image_offset(type);
		zassert_equal(
			expected_offset, actual_offset,
			"Mismatch for configured type %d. Expected: %u, Actual: %u",
			static_cast<int>(type), expected_offset, actual_offset);
	}
}

ZTEST(fpsensor_image_offset, test_cache_boundary_max_index)
{
	FpImageOffsetCache *cache = cache_fixture.get();

	zassert_equal(
		cache->get_image_offset(FP_CAPTURE_TYPE_MAX), 0,
		"Expected zero offset for invalid capture type (FP_CAPTURE_TYPE_MAX).");
}

ZTEST(fpsensor_image_offset, test_cache_boundary_negative_index)
{
	FpImageOffsetCache *cache = cache_fixture.get();

	zassert_equal(
		cache->get_image_offset(static_cast<enum fp_capture_type>(-1)),
		0, "Expected zero offset for negative capture type (-1).");
}

ZTEST(fpsensor_image_offset, test_set_image_offset)
{
	FpImageOffsetCache *cache = cache_fixture.get();
	const enum fp_capture_type test_type = FP_CAPTURE_VENDOR_FORMAT;
	const uint32_t new_offset = 64;

	/* Override offset with test helper */
	zassert_true(FpImageOffsetCacheTestHelper::set_image_offset(
			     *cache, test_type, new_offset),
		     "Failed to set image offset for test type %d", test_type);

	uint32_t updated_offset = cache->get_image_offset(test_type);
	zassert_equal(
		updated_offset, new_offset,
		"Cache failed to update with set_image_offset. Expected: %u, Actual: %u",
		new_offset, updated_offset);
}

ZTEST(fpsensor_image_offset, test_set_image_offset_boundary_max_index)
{
	FpImageOffsetCache *cache = cache_fixture.get();
	const uint32_t new_offset = 32;

	zassert_false(FpImageOffsetCacheTestHelper::set_image_offset(
		*cache, FP_CAPTURE_TYPE_MAX, new_offset));

	zassert_equal(
		cache->get_image_offset(FP_CAPTURE_TYPE_MAX), 0,
		"set_image_offset should ignore out-of-bounds FP_CAPTURE_TYPE_MAX.");
}

ZTEST(fpsensor_image_offset, test_set_image_offset_boundary_negative_index)
{
	FpImageOffsetCache *cache = cache_fixture.get();
	const uint32_t new_offset = 32;

	zassert_false(FpImageOffsetCacheTestHelper::set_image_offset(
		*cache, static_cast<enum fp_capture_type>(-1), new_offset));

	zassert_equal(
		cache->get_image_offset(static_cast<enum fp_capture_type>(-1)),
		0,
		"set_image_offset should ignore negative out-of-bounds index -1.");
}
