/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_image_frame_params.h"

#include <stddef.h>

static bool validate(const struct fp_image_frame_params_v2 &param,
		     uint32_t max_image_buffer_bytes)
{
	const uint8_t type = param.fp_capture_type;
	const uint32_t size = param.frame_size;
	const uint32_t offset = param.image_data_offset_bytes;

	/*
	 * Validate size and offset individually against the buffer
	 * size. Raw capture modes (e.g. FP_CAPTURE_VENDOR_FORMAT)
	 * report frame_size as the total buffer size inclusive of the
	 * header at image_data_offset_bytes, while shifted bounds for
	 * image-only captures are validated in get_frame().
	 */
	if (offset >= max_image_buffer_bytes) {
		CPRINTS("Error: Type %u image offset (%u) exceeds max buffer (%u).",
			type, offset, max_image_buffer_bytes);
		return false;
	}

	if (size > max_image_buffer_bytes) {
		CPRINTS("Error: Type %u frame size (%u) exceeds max allowed (%u).",
			type, size, max_image_buffer_bytes);
		return false;
	}

	if (type >= FP_CAPTURE_TYPE_MAX) {
		CPRINTS("ERROR: Invalid fp_capture_type %u received from EC, max "
			"supported is %d.",
			type, FP_CAPTURE_TYPE_MAX);
		return false;
	}

	if (size == 0) {
		CPRINTS("ERROR: Invalid 0 size for capture type %u.", type);
		return false;
	}

	return true;
}

void FpImageFrameParamsCache::populate_cache(uint32_t max_image_buffer_bytes)
{
	constexpr size_t buffer_size =
		sizeof(struct ec_response_fp_info_v3) +
		sizeof(struct fp_image_frame_params_v2) * FP_CAPTURE_TYPE_MAX;

	alignas(struct ec_response_fp_info_v3) uint8_t buffer[buffer_size]{};
	auto *info = reinterpret_cast<struct ec_response_fp_info_v3 *>(buffer);

	if (fp_sensor_get_info(info, sizeof(buffer)) < 0) {
		frame_params_.fill({});
		CPRINTS("Error: Failed to get fingerprint sensor info.");
		return;
	}

	populate_from_info(info->image_frame_params,
			   info->sensor_info.num_capture_types,
			   max_image_buffer_bytes);
}

void FpImageFrameParamsCache::populate_from_info(
	const struct fp_image_frame_params_v2 *params, uint8_t num_types,
	uint32_t max_image_buffer_bytes)
{
	frame_params_.fill({});

	if (num_types > frame_params_.size()) {
		CPRINTS("ERROR - EC returned %u types, max supported is %zu.",
			num_types, frame_params_.size());
		return;
	}

	std::array<FpImageFrameParam, FP_CAPTURE_TYPE_MAX> new_params{};
	for (uint8_t i = 0; i < num_types; ++i) {
		const auto &raw = params[i];

		if (!validate(raw, max_image_buffer_bytes)) {
			return;
		}

		new_params[raw.fp_capture_type] = {
			.frame_size_bytes = raw.frame_size,
			.image_data_offset_bytes = raw.image_data_offset_bytes,
		};
	}

	frame_params_ = new_params;
}

uint32_t
FpImageFrameParamsCache::get_frame_size(enum fp_capture_type capture_type) const
{
	if (static_cast<size_t>(capture_type) >= frame_params_.size()) {
		CPRINTS("Error: Invalid fp_capture_type %d requested (max: %zu), "
			"returning size 0.",
			capture_type, frame_params_.size());
		return 0;
	}

	if (frame_params_[capture_type].frame_size_bytes == 0) {
		CPRINTS("Error: FpImageFrameParamsCache is uninitialized or capture type %u"
			" is invalid, returning size 0.",
			capture_type);
	}

	return frame_params_[capture_type].frame_size_bytes;
}

uint32_t FpImageFrameParamsCache::get_image_offset(
	enum fp_capture_type capture_type) const
{
	if (static_cast<size_t>(capture_type) >= frame_params_.size()) {
		CPRINTS("Error: Invalid fp_capture_type %d requested (max: %zu), "
			"returning offset 0.",
			capture_type, frame_params_.size());
		return 0;
	}

	return frame_params_[capture_type].image_data_offset_bytes;
}
