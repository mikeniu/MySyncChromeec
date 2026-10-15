/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Misc utilities for fingerprint management */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_

#include "common.h"
#include "ec_commands.h"
#include "timer.h"
#include "util.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

[[nodiscard]] constexpr timestamp_t
operator+(timestamp_t ts, std::chrono::microseconds delta) noexcept
{
	return timestamp_t{
		.val = ts.val + static_cast<uint64_t>(delta.count()),
	};
}

/**
 * Return true if all bytes in |bytes| are 0x00 or all bytes are 0xff.
 */
[[nodiscard]] inline bool
bytes_are_trivial(std::span<const uint8_t> bytes) noexcept
{
	return ::bytes_are_trivial(bytes.data(), bytes.size());
}

/**
 * Test that size+offset does not exceed buffer_size
 *
 * Returns:
 *   EC_ERROR_OVERFLOW: if size+offset does not fit in uint32_t
 *   EC_ERROR_INVAL: if size+offset > buffer_size
 *   EC_SUCCESS: otherwise
 */
enum ec_error_list validate_fp_buffer_offset(uint32_t buffer_size,
					     uint32_t offset, uint32_t size);

/**
 * Return a bounds-checked subspan [offset, offset + size) of |buffer|, or
 * EC_ERROR_OVERFLOW / EC_ERROR_INVAL if offset + size overflows or exceeds
 * buffer.size().
 */
template <typename T, size_t Extent>
[[nodiscard]] inline std::expected<std::span<T>, ec_error_list>
checked_subspan(std::span<T, Extent> buffer, uint32_t offset,
		uint32_t size) noexcept
{
	if (const ec_error_list err =
		    validate_fp_buffer_offset(buffer.size(), offset, size);
	    err != EC_SUCCESS) {
		return std::unexpected(err);
	}
	return buffer.subspan(offset, size);
}

bool fp_match_success(int match_result);

/**
 * @param mode sensor mode
 * @return true if the mode is a test capture that requires finger touch.
 */
bool is_finger_needed(uint32_t mode);

/**
 * @param capture_type current capture type
 * @return true if the capture type is the one where we only care about the
 * embedded/offset image bytes to be returned over EC_CMD_FRAME.
 * Some capture types (simple, pattern0, pattern1, and reset_test) are only
 * interested in the height*width*bpp image bytes that are offset inside the
 * frame. Other modes require the full frame.
 */
bool skip_image_offset(enum fp_capture_type capture_type);

/**
 * Format an unsigned int FOURCC value as a printable string.
 *
 * If the character is unprintable, we will print '.', instead.
 * This is similar to hexdump's behavior.
 */
const std::string fourcc_to_string(uint32_t value);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_ */
