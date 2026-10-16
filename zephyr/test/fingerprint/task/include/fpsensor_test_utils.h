/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_TASK_INCLUDE_FPSENSOR_TEST_UTILS_H_
#define PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_TASK_INCLUDE_FPSENSOR_TEST_UTILS_H_

#include <zephyr/sys/printk.h>

#include <drivers/fingerprint_sim.h>
#include <fpsensor/fpsensor_image_frame_params.h>

#define FP_SIMULATOR_IMAGE_FRAME_PARAM_INITIALIZER(idx, node_id)            \
	{                                                                   \
		.frame_size = FINGERPRINT_SENSOR_FRAME_SIZE(idx, node_id),  \
		.image_data_offset_bytes =                                  \
			FINGERPRINT_SENSOR_IMAGE_OFFSET(idx, node_id),      \
		.pixel_format =                                             \
			FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(idx, node_id), \
		.width = FINGERPRINT_SENSOR_RES_X(idx, node_id),            \
		.height = FINGERPRINT_SENSOR_RES_Y(idx, node_id),           \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(idx, node_id),            \
		.fp_capture_type =                                          \
			FINGERPRINT_SENSOR_CAPTURE_TYPE(idx, node_id),      \
		.reserved = 0,                                              \
	}

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

#endif /* PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_TASK_INCLUDE_FPSENSOR_TEST_UTILS_H_ \
	*/
