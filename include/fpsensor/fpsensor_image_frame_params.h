/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_IMAGE_FRAME_PARAMS_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_IMAGE_FRAME_PARAMS_H_

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_modes.h"

#include <array>
#include <cstdint>
#include <expected>
#include <span>

/**
 * Fixed-capacity stack buffer for querying variable-length
 * ec_response_fp_info_v3 responses from fp_sensor_get_info().
 */
struct FpSensorInfoBuffer {
	struct ec_response_fp_info_v3 header{};
	std::array<struct fp_image_frame_params_v2, FP_MAX_CAPTURE_TYPES>
		params{};

	[[nodiscard]] std::expected<void, ec_error_list> query();

	[[nodiscard]] std::span<const struct fp_image_frame_params_v2>
	capture_types() const noexcept
	{
		return std::span(params).first(
			header.sensor_info.num_capture_types);
	}

	ec_response_fp_info_v3 *operator->() noexcept
	{
		return &header;
	}

	operator ec_response_fp_info_v3 *() noexcept
	{
		return &header;
	}

	[[nodiscard]] static constexpr size_t size() noexcept
	{
		return sizeof(FpSensorInfoBuffer);
	}
};
static_assert(sizeof(FpSensorInfoBuffer) ==
	      sizeof(struct ec_response_fp_info_v3) +
		      sizeof(struct fp_image_frame_params_v2) *
			      FP_MAX_CAPTURE_TYPES);

struct FpImageFrameParam {
	uint32_t frame_size_bytes = 0;
};

class FpImageFrameParamsCache {
    public:
	FpImageFrameParamsCache() = default;

	/**
	 * @brief Internal method to populate the frame size array.
	 *
	 * On failure, this method invalidates the entire frame_sizes_ array by
	 * filling it with zeros.
	 *
	 * @param max_frame_size_bytes The maximum allowable size for any
	 * fingerprint frame, used for validation.
	 *
	 */
	void populate_cache(uint32_t max_frame_size_bytes);

	/**
	 * @brief Looks up the frame size for a validated capture type.
	 *
	 * @param capture_type The validated FpCaptureType to look up.
	 *
	 * @return The frame size (uint32_t) if found, or EC_ERROR_INVAL if the
	 * size is 0 or the cache is uninitialized.
	 */
	[[nodiscard]] std::expected<uint32_t, ec_error_list>
	get_frame_size(FpCaptureType capture_type) const;

	/**
	 * @brief Looks up the frame size for a given capture type.
	 *
	 * @param capture_type The enum fp_capture_type to look up.
	 *
	 * @return The frame size (uint32_t) if found, or EC_ERROR_INVAL
	 * otherwise (e.g., for an invalid type or if size is 0, or if the cache
	 * is uninitialized).
	 */
	[[nodiscard]] std::expected<uint32_t, ec_error_list>
	get_frame_size(enum fp_capture_type capture_type) const;

    private:
	std::array<FpImageFrameParam, FP_CAPTURE_TYPE_MAX> frame_params_ = {};

#ifdef CONFIG_ZTEST
	friend class FpImageFrameParamsCacheTestHelper;
#endif
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_IMAGE_FRAME_PARAMS_H_ */
