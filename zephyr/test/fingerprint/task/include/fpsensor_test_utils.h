/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __TASK_FPSENSOR_TEST_UTILS_H
#define __TASK_FPSENSOR_TEST_UTILS_H

#include <zephyr/sys/printk.h>

#include <fpsensor/fpsensor_frame_size.h>

class FpFrameSizeCacheTestHelper {
    public:
	static void set_frame_size(FpFrameSizeCache &cache,
				   enum fp_capture_type capture_type,
				   uint32_t size)
	{
		if (capture_type < 0 || static_cast<size_t>(capture_type) >=
						cache.frame_sizes_.size()) {
			printk("Error: Invalid capture type %d\n",
			       capture_type);
			return;
		}
		cache.frame_sizes_[static_cast<size_t>(capture_type)] = size;
	}
};

#endif /* __TASK_FPSENSOR_TEST_UTILS_H */
