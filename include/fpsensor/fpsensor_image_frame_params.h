/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_IMAGE_FRAME_PARAMS_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_IMAGE_FRAME_PARAMS_H_

#include "ec_commands.h"

#include <array>
#include <cstdint>
#include <optional>

struct FpImageFrameParam {
	uint32_t frame_size_bytes = 0;
	uint32_t image_data_offset_bytes = 0;
};

class FpImageFrameParamsCache {
    public:
	FpImageFrameParamsCache() = default;

	/* No copying or moving of this object. */
	FpImageFrameParamsCache(const FpImageFrameParamsCache &) = delete;
	FpImageFrameParamsCache &
	operator=(const FpImageFrameParamsCache &) = delete;
	FpImageFrameParamsCache(FpImageFrameParamsCache &&) = delete;
	FpImageFrameParamsCache &operator=(FpImageFrameParamsCache &&) = delete;

	/**
	 * @brief Populates the frame parameters cache from the EC.
	 *
	 * Queries the EC via fp_sensor_get_info() and caches frame parameters
	 * (frame size and image data offset) indexed by capture type.
	 *
	 * On failure, this method invalidates the entire frame_params_ array by
	 * filling it with zeros.
	 *
	 * @param max_image_buffer_bytes Maximum allowable capacity of the
	 * underlying image/frame buffer, used for bounds validation.
	 */
	void populate_cache(uint32_t max_image_buffer_bytes);

	/**
	 * @brief Looks up the frame parameters for a given capture type.
	 *
	 * @param capture_type The enum fp_capture_type to look up.
	 * @return FpImageFrameParam if valid and configured, std::nullopt
	 * otherwise.
	 */
	std::optional<FpImageFrameParam>
	get(enum fp_capture_type capture_type) const;

    private:
	void populate_from_info(const struct ec_response_fp_info_v3 &info,
				uint32_t max_image_buffer_bytes);

	std::array<FpImageFrameParam, FP_CAPTURE_TYPE_MAX> frame_params_ = {};

#ifdef CONFIG_ZTEST
	friend class FpImageFrameParamsCacheTestHelper;
#endif
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_IMAGE_FRAME_PARAMS_H_ */
