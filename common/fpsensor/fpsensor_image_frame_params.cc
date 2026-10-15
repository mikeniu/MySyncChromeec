/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_image_frame_params.h"

#include <stddef.h>

#include <span>
#include <vector>

namespace
{

bool validate_frame_param(const fp_image_frame_params_v2 &param,
			  uint32_t max_frame_size_bytes,
			  size_t max_capture_types)
{
	const uint8_t type = param.fp_capture_type;
	const uint32_t size = param.frame_size;

	if (size > max_frame_size_bytes) {
		CPRINTS("Error: Type %u frame size (%u) exceeds max allowed (%u).",
			type, size, max_frame_size_bytes);
		return false;
	}

	if (type >= max_capture_types) {
		CPRINTS("ERROR: Invalid fp_capture_type %u received from EC, max "
			"supported is %zu.",
			type, max_capture_types);
		return false;
	}

	if (size == 0) {
		CPRINTS("ERROR: Invalid 0 size for capture type %u.", type);
		return false;
	}

	return true;
}

} /* namespace */

void FpImageFrameParamsCache::populate_cache(uint32_t max_frame_size_bytes)
{
	const size_t buffer_size =
		sizeof(struct ec_response_fp_info_v3) +
		sizeof(struct fp_image_frame_params_v2) * frame_params_.size();

	std::vector<uint8_t> buffer(buffer_size);
	auto *info = reinterpret_cast<struct ec_response_fp_info_v3 *>(
		buffer.data());

	if (fp_sensor_get_info(info, buffer.size()) < 0) {
		CPRINTS("Error: Failed to get fingerprint sensor info.");
		return;
	}

	const uint8_t num_types = info->sensor_info.num_capture_types;

	if (num_types > frame_params_.size()) {
		CPRINTS("ERROR - EC returned %u types, max supported is %zu.",
			num_types, frame_params_.size());
		return;
	}

	for (const auto &param :
	     std::span(info->image_frame_params, num_types)) {
		if (!validate_frame_param(param, max_frame_size_bytes,
					  frame_params_.size())) {
			/* Invalidate cache. */
			frame_params_.fill({});
			return;
		}

		frame_params_[param.fp_capture_type] = {
			.frame_size_bytes = param.frame_size,
		};
	}
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
