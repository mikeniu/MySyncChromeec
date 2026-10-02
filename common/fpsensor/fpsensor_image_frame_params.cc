/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_image_frame_params.h"

#include <stddef.h>

#include <vector>

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

	auto *params = info->image_frame_params;

	for (uint8_t i = 0; i < num_types; ++i) {
		const uint8_t type = params[i].fp_capture_type;
		const uint32_t size = params[i].frame_size;

		if (size > max_frame_size_bytes) {
			CPRINTS("Error: Type %u frame size (%u) exceeds max allowed (%u).",
				type, size, max_frame_size_bytes);
			goto error_exit;
		}

		if (type >= frame_params_.size()) {
			CPRINTS("ERROR: Invalid fp_capture_type %u received from EC, max "
				"supported is %zu.",
				type, frame_params_.size());
			goto error_exit;
		}

		if (size == 0) {
			CPRINTS("ERROR: Invalid 0 size for capture type %u.",
				type);
			goto error_exit;
		}

		frame_params_[type] = {
			.frame_size_bytes = size,
		};
	}

	return;

error_exit:
	/* Invalidate cache. */
	frame_params_.fill({});
	return;
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
