/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Misc utilities for fingerprint management */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_

#include "common.h"
#include "ec_commands.h"
#include "overflow.h"
#include "timer.h"
#include "util.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

struct FpClock {
	using rep = int64_t;
	using period = std::micro;
	using duration = std::chrono::microseconds;
	using time_point = std::chrono::time_point<FpClock>;
	static constexpr bool is_steady = true;

	[[nodiscard]] static time_point now() noexcept
	{
		return time_point{ duration{ get_time().val } };
	}
};

using FpTimestamp = FpClock::time_point;

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
[[nodiscard]] constexpr enum ec_error_list
validate_fp_buffer_offset(uint32_t buffer_size, uint32_t offset,
			  uint32_t size) noexcept
{
	uint32_t bytes_requested = 0;

	if (check_add_overflow(size, offset, &bytes_requested))
		return EC_ERROR_OVERFLOW;

	if (bytes_requested > buffer_size)
		return EC_ERROR_INVAL;

	return EC_SUCCESS;
}

/**
 * Return a bounds-checked subspan [offset, offset + size) of |buffer|, or
 * EC_ERROR_OVERFLOW / EC_ERROR_INVAL if offset + size overflows or exceeds
 * buffer.size().
 */
template <typename T, size_t Extent>
[[nodiscard]] constexpr std::expected<std::span<T>, ec_error_list>
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

[[nodiscard]] constexpr bool fp_match_success(int match_result) noexcept
{
	return match_result == EC_MKBP_FP_ERR_MATCH_YES ||
	       match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATED ||
	       match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED;
}

/**
 * @param mode sensor mode
 * @return true if the mode is a test capture that requires finger touch.
 */
[[nodiscard]] constexpr bool is_finger_needed(uint32_t mode) noexcept
{
	const int capture_type = FP_CAPTURE_TYPE(mode);

	return !((mode & FP_MODE_CAPTURE) &&
		 (capture_type == FP_CAPTURE_DEFECT_PXL_TEST ||
		  capture_type == FP_CAPTURE_ABNORMAL_TEST ||
		  capture_type == FP_CAPTURE_NOISE_TEST ||
		  capture_type == FP_CAPTURE_PATTERN0 ||
		  capture_type == FP_CAPTURE_PATTERN1 ||
		  capture_type == FP_CAPTURE_RESET_TEST ||
		  capture_type == FP_CAPTURE_PATTERN2));
}

/**
 * @param capture_type current capture type
 * @return true if the capture type is the one where we only care about the
 * embedded/offset image bytes to be returned over EC_CMD_FRAME.
 * Some capture types (simple, pattern0, pattern1, and reset_test) are only
 * interested in the height*width*bpp image bytes that are offset inside the
 * frame. Other modes require the full frame.
 */
[[nodiscard]] constexpr bool
skip_image_offset(enum fp_capture_type capture_type) noexcept
{
	return !(capture_type == FP_CAPTURE_VENDOR_FORMAT ||
		 capture_type == FP_CAPTURE_QUALITY_TEST);
}

/**
 * Format an unsigned int FOURCC value as a printable string.
 *
 * If the character is unprintable, we will print '.', instead.
 * This is similar to hexdump's behavior.
 */
std::string fourcc_to_string(uint32_t value);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_ */
