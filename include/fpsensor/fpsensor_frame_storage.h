/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint raw frame buffer and frame parameter cache storage */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_FRAME_STORAGE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_FRAME_STORAGE_H_

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_image_frame_params.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_utils.h"
#include "fpsensor_driver.h"
#include "fpsensor_state_driver.h"
#include "link_defs.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

/* if no special memory regions are defined, fallback on regular SRAM */
#ifndef FP_FRAME_SECTION
#define FP_FRAME_SECTION
#endif

/**
 * Domain storage for captured raw fingerprint frames and frame parameter cache.
 */
struct FpFrameStorage {
	/** Raw frame buffer. */
	std::span<uint8_t, FP_SENSOR_IMAGE_SIZE> image_buffer = fp_buffer;
	/**
	 * Type of the image captured from the fingerprint sensor in the
	 * most recent successful capture.
	 */
	std::optional<FpCaptureType> current_capture_type = std::nullopt;
	/** Map of the capture type to frame parameters. */
	FpImageFrameParamsCache frame_params_cache{};

	void init_cache()
	{
		frame_params_cache.populate_cache(image_buffer.size());
	}

	constexpr void begin_capture() noexcept
	{
		current_capture_type.reset();
	}

	constexpr void finish_capture(FpCaptureType type) noexcept
	{
		current_capture_type = type;
	}

	void clear() noexcept
	{
		cleanse(image_buffer);
		current_capture_type.reset();
	}

	[[nodiscard]] constexpr
		std::span<uint8_t, FP_SENSOR_IMAGE_SIZE - FP_SENSOR_IMAGE_OFFSET>
		image_data() const noexcept
	{
		return image_buffer.subspan<FP_SENSOR_IMAGE_OFFSET>();
	}

	/**
	 * Return the active captured-frame slice of |image_buffer| for the most
	 * recent capture, or EC_RES_INVALID_PARAM if no valid frame is
	 * available.
	 */
	[[nodiscard]] std::expected<std::span<const uint8_t>, ec_status>
	captured_frame() const
	{
		if (!current_capture_type.has_value()) {
			return std::unexpected(EC_RES_INVALID_PARAM);
		}
		const uint32_t image_offset =
			current_capture_type->skip_image_offset() ?
				FP_SENSOR_IMAGE_OFFSET :
				0;
		const std::span<const uint8_t> buf = image_buffer;
		return frame_params_cache.get_frame_size(*current_capture_type)
			.and_then([buf, image_offset](uint32_t frame_size) {
				return checked_subspan(buf, image_offset,
						       frame_size);
			})
			.transform_error([](ec_error_list) {
				return EC_RES_INVALID_PARAM;
			});
	}

	[[nodiscard]] std::expected<void, ec_status>
	get_raw_frame(uint32_t offset, std::span<uint8_t> output) const;
	[[nodiscard]] std::expected<void, ec_error_list>
	upload_hex_chunk(uint32_t offset, std::string_view hex_pixels);
	[[nodiscard]] std::expected<void, ec_error_list> stream_pgm() const;
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_FRAME_STORAGE_H_ */
