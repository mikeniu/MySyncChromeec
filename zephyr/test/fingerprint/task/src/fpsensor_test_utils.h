/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>

#include <fpsensor/fpsensor_frame_size.h>

LOG_MODULE_REGISTER(fpsensor_test_utils, LOG_LEVEL_INF);

class FpFrameSizeCacheTestHelper {
    public:
	/**
	 * @brief Test-only method to manually override frame size values.
	 *
	 * @param capture_type The `fp_capture_type` to modify.
	 * @param size The size in bytes to write to the cache. If the
	 * capture_type is out of bounds, this function logs an error and
	 * performs no operation.
	 */
	static void set_frame_size(FpFrameSizeCache &cache,
				   enum fp_capture_type capture_type,
				   uint32_t size)
	{
		if (capture_type < 0 || static_cast<size_t>(capture_type) >=
						cache.frame_sizes_.size()) {
			LOG_ERR("Error: Invalid capture type %d\n",
				capture_type);
			return;
		}

		cache.frame_sizes_[static_cast<size_t>(capture_type)] = size;
	}
};
