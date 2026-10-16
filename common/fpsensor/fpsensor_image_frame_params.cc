/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_image_frame_params.h"
#include "fpsensor/fpsensor_modes.h"

#include <cstddef>
#include <expected>
#include <span>
#include <utility>

namespace
{

std::expected<std::pair<FpCaptureType, FpImageFrameParam>, ec_error_list>
parse_frame_param(const fp_image_frame_params_v2 &param,
		  uint32_t max_frame_size_bytes)
{
	const uint8_t type = param.fp_capture_type;
	const uint32_t size = param.frame_size;

	if (size > max_frame_size_bytes) {
		CPRINTS("Error: Type %u frame size (%u) exceeds max allowed (%u).",
			type, size, max_frame_size_bytes);
		return std::unexpected(EC_ERROR_INVAL);
	}

	const auto capture_type = FpCaptureType::create(type);
	if (!capture_type.has_value()) {
		CPRINTS("ERROR: Invalid fp_capture_type %u received from EC, max "
			"supported is %zu.",
			type, static_cast<size_t>(FP_CAPTURE_TYPE_MAX));
		return std::unexpected(EC_ERROR_INVAL);
	}

	if (size == 0) {
		CPRINTS("ERROR: Invalid 0 size for capture type %u.", type);
		return std::unexpected(EC_ERROR_INVAL);
	}

	return std::pair{ *capture_type,
			  FpImageFrameParam{ .frame_size_bytes = size } };
}

} /* namespace */

std::expected<std::span<const struct fp_image_frame_params_v2>, ec_error_list>
FpSensorInfoBuffer::query()
{
	*this = {};
	if (fp_sensor_get_info(&header, sizeof(*this)) < 0) {
		return std::unexpected(EC_ERROR_UNKNOWN);
	}
	if (header.sensor_info.num_capture_types > params.size()) {
		CPRINTS("ERROR - EC returned %u types, max supported is %zu.",
			header.sensor_info.num_capture_types, params.size());
		return std::unexpected(EC_ERROR_INVAL);
	}
	return capture_types();
}

void FpImageFrameParamsCache::populate_cache(uint32_t max_frame_size_bytes)
{
	FpSensorInfoBuffer buffer{};
	const auto types = buffer.query();
	if (!types.has_value()) {
		CPRINTS("Error: Failed to get fingerprint sensor info.");
		return;
	}

	for (const auto &param : *types) {
		const auto parsed =
			parse_frame_param(param, max_frame_size_bytes);
		if (!parsed.has_value()) {
			/* Invalidate cache. */
			frame_params_.fill({});
			return;
		}

		frame_params_[parsed->first.raw()] = parsed->second;
	}
}

std::expected<uint32_t, ec_error_list>
FpImageFrameParamsCache::get_frame_size(FpCaptureType capture_type) const
{
	const uint32_t size =
		frame_params_[capture_type.raw()].frame_size_bytes;
	if (size == 0) {
		CPRINTS("Error: FpImageFrameParamsCache is uninitialized or capture type %u"
			" is invalid, returning size 0.",
			capture_type.raw());
		return std::unexpected(EC_ERROR_INVAL);
	}

	return size;
}

std::expected<uint32_t, ec_error_list>
FpImageFrameParamsCache::get_frame_size(enum fp_capture_type capture_type) const
{
	return FpCaptureType::create(capture_type)
		.or_else([this, capture_type](ec_error_list err)
				 -> std::expected<FpCaptureType, ec_error_list> {
			CPRINTS("Error: Invalid fp_capture_type %d requested (max: %zu), "
				"returning size 0.",
				capture_type, frame_params_.size());
			return std::unexpected(err);
		})
		.and_then([this](FpCaptureType valid_type) {
			return get_frame_size(valid_type);
		});
}
