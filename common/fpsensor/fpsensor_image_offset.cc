/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_image_offset.h"

#include <stddef.h>

#include <vector>

void FpImageOffsetCache::populate_cache(uint32_t max_image_offset_bytes)
{
	constexpr size_t buffer_size =
		sizeof(struct ec_response_fp_info_v3) +
		sizeof(struct fp_image_frame_params_v2) * FP_CAPTURE_TYPE_MAX;

	uint8_t buffer[buffer_size] = {};
	auto *info = reinterpret_cast<struct ec_response_fp_info_v3 *>(buffer);

	if (fp_sensor_get_info(info, buffer_size) < 0) {
		CPRINTF("Error: Failed to get fingerprint sensor info.\n");
		return;
	}

	const uint8_t num_types = info->sensor_info.num_capture_types;

	if (num_types > image_offsets_.size()) {
		CPRINTF("ERROR - EC returned %u types, max supported is %zu.\n",
			num_types, image_offsets_.size());
		return;
	}

	auto *params = info->image_frame_params;

	for (uint8_t i = 0; i < num_types; ++i) {
		const uint8_t type = params[i].fp_capture_type;
		const uint32_t offset = params[i].image_data_offset_bytes;

		if (offset > max_image_offset_bytes) {
			CPRINTF("Error: Type %u image offset (%u) exceeds max allowed (%u).\n",
				type, offset, max_image_offset_bytes);
			goto error_exit;
		}

		if (type >= image_offsets_.size()) {
			CPRINTF("ERROR: Invalid fp_capture_type %u received from EC, max "
				"supported is %zu.\n",
				type, image_offsets_.size());
			goto error_exit;
		}

		image_offsets_[type] = offset;
	}

	return;

error_exit:
	/* Invalidate cache. */
	image_offsets_.fill(0);
	return;
}

uint32_t
FpImageOffsetCache::get_image_offset(enum fp_capture_type capture_type) const
{
	if (static_cast<size_t>(capture_type) >= image_offsets_.size()) {
		CPRINTF("Error: Invalid fp_capture_type %d requested (max: %zu), "
			"returning offset 0.\n",
			capture_type, image_offsets_.size());
		return 0;
	}

	return image_offsets_[capture_type];
}
