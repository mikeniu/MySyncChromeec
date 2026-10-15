/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_image_frame_params.h"
#include "fpsensor/fpsensor_modes.h"

#include <stddef.h>

#include <span>

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

	if (!FpCaptureType::create(type).has_value() ||
	    type >= max_capture_types) {
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
	FpSensorInfoBuffer buffer{};

	if (fp_sensor_get_info(&buffer.header, sizeof(buffer)) < 0) {
		CPRINTS("Error: Failed to get fingerprint sensor info.");
		return;
	}

	const uint8_t num_types = buffer.header.sensor_info.num_capture_types;

	if (num_types > frame_params_.size()) {
		CPRINTS("ERROR - EC returned %u types, max supported is %zu.",
			num_types, frame_params_.size());
		return;
	}

	for (const auto &param : buffer.capture_types()) {
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
	const auto valid_type = FpCaptureType::create(capture_type);
	if (!valid_type.has_value() ||
	    static_cast<size_t>(valid_type->raw()) >= frame_params_.size()) {
		CPRINTS("Error: Invalid fp_capture_type %d requested (max: %zu), "
			"returning size 0.",
			capture_type, frame_params_.size());
		return 0;
	}

	if (frame_params_[valid_type->raw()].frame_size_bytes == 0) {
		CPRINTS("Error: FpImageFrameParamsCache is uninitialized or capture type %u"
			" is invalid, returning size 0.",
			valid_type->raw());
	}

	return frame_params_[valid_type->raw()].frame_size_bytes;
}
