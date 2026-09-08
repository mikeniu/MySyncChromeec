/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_TASK_INCLUDE_FPSENSOR_TEST_UTILS_H_
#define PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_TASK_INCLUDE_FPSENSOR_TEST_UTILS_H_

#include <zephyr/sys/printk.h>

#include <fpsensor/fpsensor_image_frame_params.h>

class FpImageFrameParamsCacheTestHelper {
    public:
	FpImageFrameParamsCacheTestHelper() = delete;

	static bool set_frame_size(FpImageFrameParamsCache &cache,
				   enum fp_capture_type capture_type,
				   uint32_t size)
	{
		if (static_cast<size_t>(capture_type) >=
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

using FpFrameSizeCacheTestHelper = FpImageFrameParamsCacheTestHelper;

#endif /* PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_TASK_INCLUDE_FPSENSOR_TEST_UTILS_H_ \
	*/
