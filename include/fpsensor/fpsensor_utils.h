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

#ifdef is_negative
#undef is_negative
#endif

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

#if !defined(CONFIG_ZEPHYR) && !defined(EC_HOST_CMD_HANDLER)
#include "atomic_bit.h"
#include "host_command.h"

struct ec_host_cmd_handler_args {
	const void *send_response;
	uint16_t command;
	uint8_t version;
	const void *input_buf;
	uint16_t input_buf_size;
	void *output_buf;
	uint16_t output_buf_max;
	uint16_t output_buf_size;
	uint16_t result;
};

#define ec_host_cmd_status ec_status
#define EC_HOST_CMD_SUCCESS EC_RES_SUCCESS
#define EC_HOST_CMD_ERROR EC_RES_ERROR
#define EC_HOST_CMD_INVALID_PARAM EC_RES_INVALID_PARAM
#define EC_HOST_CMD_ACCESS_DENIED EC_RES_ACCESS_DENIED
#define EC_HOST_CMD_UNAVAILABLE EC_RES_UNAVAILABLE
#define EC_HOST_CMD_TIMEOUT EC_RES_TIMEOUT
#define EC_HOST_CMD_OVERFLOW EC_RES_OVERFLOW
#define EC_HOST_CMD_REQUEST_TRUNCATED EC_RES_REQUEST_TRUNCATED
#define EC_HOST_CMD_RESPONSE_TOO_BIG EC_RES_RESPONSE_TOO_BIG
#define EC_HOST_CMD_BUSY EC_RES_BUSY

#define EC_HOST_CMD_HANDLER_UNBOUND(command, function, version_mask)         \
	static enum ec_status _host_cmd_shim_##function(                     \
		struct host_cmd_handler_args *args)                          \
	{                                                                    \
		return function(                                             \
			reinterpret_cast<ec_host_cmd_handler_args *>(args)); \
	}                                                                    \
	DECLARE_HOST_COMMAND(command, _host_cmd_shim_##function, version_mask)

#define EC_HOST_CMD_HANDLER(cmd, fn, ver, req, resp) \
	EC_HOST_CMD_HANDLER_UNBOUND(cmd, fn, ver)
#define EC_HOST_CMD_HANDLER_REQ_ONLY(cmd, fn, ver, req) \
	EC_HOST_CMD_HANDLER_UNBOUND(cmd, fn, ver)
#define EC_HOST_CMD_HANDLER_RESP_ONLY(cmd, fn, ver, resp) \
	EC_HOST_CMD_HANDLER_UNBOUND(cmd, fn, ver)
#define SMALLEST_TYPE(T1, T2) \
	std::conditional_t<sizeof(T1) < sizeof(T2), T1, T2>
#endif

/**
 * Test that size+offset does not exceed buffer_size
 *
 * Returns:
 *   EC_ERROR_OVERFLOW: if size+offset does not fit in uint32_t
 *   EC_ERROR_INVAL: if size+offset > buffer_size
 *   EC_SUCCESS: otherwise
 */
[[nodiscard]] constexpr enum ec_error_list
validate_fp_buffer_offset(const uint32_t buffer_size, const uint32_t offset,
			  const uint32_t size) noexcept
{
	uint32_t bytes_requested = 0;

	if (check_add_overflow(size, offset, &bytes_requested)) {
		return EC_ERROR_OVERFLOW;
	}

	if (bytes_requested > buffer_size) {
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

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

[[nodiscard]] constexpr bool fp_match_success(const int match_result) noexcept
{
	return match_result == EC_MKBP_FP_ERR_MATCH_YES ||
	       match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATED ||
	       match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED;
}

class FpFourCc {
    public:
	explicit constexpr FpFourCc(uint32_t value) noexcept : chars_{}
	{
		for (size_t i = 0; i < sizeof(value); ++i) {
			const uint8_t ch = (value >> (i * 8)) & 0xFF;
			chars_[i] = (ch >= 0x20 && ch <= 0x7e) ?
					    static_cast<char>(ch) :
					    '.';
		}
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

[[nodiscard]] constexpr FpFourCc fourcc_to_string(uint32_t value) noexcept
{
	return FpFourCc(value);
}

#include "fpsensor/fpsensor_modes.h"

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_UTILS_H_ */
