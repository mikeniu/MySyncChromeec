/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_IMAGE_FRAME_PARAMS_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_IMAGE_FRAME_PARAMS_H_

#include "ec_commands.h"

#include <array>
#include <cstdint>

struct FpImageFrameParam {
	uint32_t frame_size_bytes = 0;
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
	 * (currently frame size) indexed by capture type.
	 *
	 * @param max_frame_size_bytes Maximum permissible frame buffer size.
	 */
	void populate_cache(uint32_t max_frame_size_bytes);

	uint32_t get_frame_size(enum fp_capture_type capture_type) const;

    private:
	std::array<FpImageFrameParam, FP_CAPTURE_TYPE_MAX> frame_params_ = {};

#ifdef CONFIG_ZTEST
	friend class FpImageFrameParamsCacheTestHelper;
#endif
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_IMAGE_FRAME_PARAMS_H_ */
