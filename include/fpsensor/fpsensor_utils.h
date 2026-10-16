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
 *   std::unexpected(EC_ERROR_OVERFLOW): if size+offset does not fit in uint32_t
 *   std::unexpected(EC_ERROR_INVAL): if size+offset > buffer_size
 *   {}: otherwise
 */
[[nodiscard]] constexpr std::expected<void, ec_error_list>
validate_fp_buffer_offset(uint32_t buffer_size, uint32_t offset,
			  uint32_t size) noexcept
{
	uint32_t bytes_requested = 0;

	if (check_add_overflow(size, offset, &bytes_requested))
		return std::unexpected(EC_ERROR_OVERFLOW);

	if (bytes_requested > buffer_size)
		return std::unexpected(EC_ERROR_INVAL);

	return {};
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
	return validate_fp_buffer_offset(buffer.size(), offset, size)
		.transform([buffer, offset, size] {
			return buffer.subspan(offset, size);
		});
}

[[nodiscard]] constexpr bool fp_match_success(int match_result) noexcept
{
	return match_result == EC_MKBP_FP_ERR_MATCH_YES ||
	       match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATED ||
	       match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED;
}

/**
 * Format an unsigned int FOURCC value as a printable string.
 *
 * If the character is unprintable, we will print '.', instead.
 * This is similar to hexdump's behavior.
 */
std::string fourcc_to_string(uint32_t value);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_ */
