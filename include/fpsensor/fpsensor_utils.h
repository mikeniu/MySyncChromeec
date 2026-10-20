/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Misc utilities for fingerprint management */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_

#include "common.h"
#include "ec_commands.h"
#include "host_command.h"
#include "overflow.h"
#include "timer.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

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
 * Rate limiter enforcing a minimum cooldown interval between operations
 * (e.g., at most 1 template encryption per second, see b/114160734).
 */
struct FpRateLimiter {
	FpClock::duration cooldown = std::chrono::seconds(1);
	FpTimestamp deadline{};

	[[nodiscard]] constexpr std::expected<void, ec_host_cmd_status>
	try_acquire(FpTimestamp now = FpClock::now()) noexcept
	{
		if (now < deadline) {
			return std::unexpected(EC_HOST_CMD_BUSY);
		}
		deadline = now + cooldown;
		return {};
	}
};

/**
 * Securely zero out all bytes in |bytes|.
 */
void cleanse(std::span<uint8_t> bytes) noexcept;

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

	if (check_add_overflow(size, offset, &bytes_requested)) {
		return std::unexpected(EC_ERROR_OVERFLOW);
	}

	if (bytes_requested > buffer_size) {
		return std::unexpected(EC_ERROR_INVAL);
	}

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
 * Printable 4-character FOURCC identifier (non-printable bytes replaced with
 * '.', similar to hexdump).
 */
class FpFourCc {
    public:
	explicit constexpr FpFourCc(uint32_t value) noexcept : chars_{}
	{
		const auto bytes =
			std::bit_cast<std::array<uint8_t, sizeof(value)> >(
				value);
		std::ranges::transform(bytes, chars_.begin(), [](uint8_t ch) {
			return (ch >= 0x20 && ch <= 0x7e) ?
				       static_cast<char>(ch) :
				       '.';
		});
	}

	[[nodiscard]] constexpr const char *c_str() const noexcept
	{
		return chars_.data();
	}

	[[nodiscard]] constexpr std::string_view view() const noexcept
	{
		return { chars_.data(), sizeof(uint32_t) };
	}

    private:
	std::array<char, sizeof(uint32_t) + 1> chars_;
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_ */
