/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_IMAGE_OFFSET_H
#define __CROS_EC_FPSENSOR_FPSENSOR_IMAGE_OFFSET_H

#include "ec_commands.h"

#include <array>
#include <cstdint>

class FpImageOffsetCache {
    public:
	FpImageOffsetCache() = default;

	/* No copying or moving of this object. */
	FpImageOffsetCache(const FpImageOffsetCache &) = delete;
	FpImageOffsetCache &operator=(const FpImageOffsetCache &) = delete;
	FpImageOffsetCache(FpImageOffsetCache &&) = delete;
	FpImageOffsetCache &operator=(FpImageOffsetCache &&) = delete;

	/**
	 * @brief Internal method to populate the image offset array.
	 *
	 * Queries the EC via fp_sensor_get_info() and caches the image data
	 * offset bytes for each supported capture type. On failure, this
	 * method invalidates the cache by filling it with zeros.
	 *
	 * @param max_image_offset_bytes The maximum allowable image data offset
	 * for any fingerprint frame, used for bounds validation.
	 */
	void populate_cache(uint32_t max_image_offset_bytes);

	/**
	 * @brief Looks up the image data offset in bytes for a given capture
	 * type.
	 *
	 * @param capture_type The enum fp_capture_type to look up.
	 *
	 * @return The image data offset (uint32_t) if found, or 0 otherwise
	 * (e.g., if capture_type is invalid or out of bounds).
	 */
	uint32_t get_image_offset(enum fp_capture_type capture_type) const;

    private:
	std::array<uint32_t, FP_CAPTURE_TYPE_MAX> image_offsets_ = {};

#ifdef CONFIG_ZTEST
	friend class FpImageOffsetCacheTestHelper;
#endif
};

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_IMAGE_OFFSET_H */
