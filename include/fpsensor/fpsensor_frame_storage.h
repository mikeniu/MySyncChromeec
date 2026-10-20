/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint raw frame buffer and frame parameter cache accessors */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_FRAME_STORAGE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_FRAME_STORAGE_H_

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_image_frame_params.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_utils.h"
#include "fpsensor_driver.h"
#include "system.h"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <span>

struct FpFrameStorage {
	/**
	 * Return the active captured-frame slice of |image_buffer| for
	 * |capture_type|, or EC_RES_INVALID_PARAM if no valid frame is
	 * available.
	 */
	[[nodiscard]] static std::expected<std::span<const uint8_t>, ec_status>
	captured_frame(std::span<const uint8_t> image_buffer,
		       enum fp_capture_type capture_type,
		       const FpImageFrameParamsCache &cache)
	{
		const auto type = FpCaptureType::create(capture_type);
		if (!type.has_value()) {
			return std::unexpected(EC_RES_INVALID_PARAM);
		}
		const uint32_t frame_size = cache.get_frame_size(type->raw());
		if (frame_size == 0 || frame_size > image_buffer.size()) {
			return std::unexpected(EC_RES_INVALID_PARAM);
		}
		const uint32_t image_offset =
			type->skip_image_offset() ? FP_SENSOR_IMAGE_OFFSET : 0;
		return checked_subspan(image_buffer, image_offset, frame_size)
			.transform_error([](ec_error_list) {
				return EC_RES_INVALID_PARAM;
			});
	}

	[[nodiscard]] static std::expected<void, ec_status>
	get_raw_frame(std::span<const uint8_t> image_buffer,
		      enum fp_capture_type capture_type,
		      const FpImageFrameParamsCache &cache, uint32_t offset,
		      std::span<uint8_t> output)
	{
		if (system_is_locked()) {
			return std::unexpected(EC_RES_ACCESS_DENIED);
		}
		return captured_frame(image_buffer, capture_type, cache)
			.and_then([offset,
				   output](std::span<const uint8_t> captured) {
				return checked_subspan(captured, offset,
						       output.size())
					.transform_error([](ec_error_list) {
						return EC_RES_INVALID_PARAM;
					});
			})
			.transform([output](std::span<const uint8_t> chunk) {
				std::ranges::copy(chunk, output.begin());
			});
	}
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_FRAME_STORAGE_H_ */
