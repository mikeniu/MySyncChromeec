/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef TASK_FPSENSOR_TEST_UTILS_H
#define TASK_FPSENSOR_TEST_UTILS_H

#include <zephyr/sys/printk.h>

#include <fpsensor/fpsensor_image_frame_params.h>

class FpImageFrameParamsCacheTestHelper {
    public:
	FpImageFrameParamsCacheTestHelper() = delete;

	static bool set_frame_size(FpImageFrameParamsCache &cache,
				   enum fp_capture_type capture_type,
				   uint32_t size)
	{
		if (capture_type < 0 || static_cast<size_t>(capture_type) >=
						cache.frame_params_.size()) {
			printk("Error: Invalid capture type %d\n",
			       capture_type);
			return false;
		}
		cache.frame_params_[static_cast<size_t>(capture_type)]
			.frame_size_bytes = size;
		return true;
	}
};

#endif /* TASK_FPSENSOR_TEST_UTILS_H */
