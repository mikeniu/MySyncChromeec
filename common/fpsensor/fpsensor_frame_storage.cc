/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_frame_storage.h"
#include "fpsensor/fpsensor_utils.h"
#include "fpsensor_driver.h"
#include "link_defs.h"
#include "system.h"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <span>

#pragma GCC poison global_context

#if defined(CONFIG_CROS_EC_RO)
#error "fpsensor code should not be in RO image."
#endif

/* Last acquired frame (aligned as it is used by arbitrary binary libraries) */
uint8_t fp_buffer[FP_SENSOR_IMAGE_SIZE] FP_FRAME_SECTION __aligned(4);

std::expected<void, ec_status>
FpFrameStorage::get_raw_frame(uint32_t offset, std::span<uint8_t> output) const
{
	if (system_is_locked()) {
		return std::unexpected(EC_RES_ACCESS_DENIED);
	}

	const auto captured = captured_frame();
	if (!captured.has_value()) {
		return std::unexpected(captured.error());
	}

	return checked_subspan(*captured, offset, output.size())
		.transform([output](std::span<const uint8_t> chunk) {
			std::ranges::copy(chunk, output.begin());
		})
		.transform_error(
			[](ec_error_list) { return EC_RES_INVALID_PARAM; });
}
