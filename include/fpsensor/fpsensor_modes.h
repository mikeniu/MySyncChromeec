/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_

#include "ec_commands.h"

#include <bit>
#include <cstdint>

inline constexpr uint32_t FP_MODE_ANY_CAPTURE =
	(FP_MODE_CAPTURE | FP_MODE_ENROLL_IMAGE | FP_MODE_MATCH);

inline constexpr uint32_t FP_MODE_ANY_DETECT_FINGER =
	(FP_MODE_FINGER_DOWN | FP_MODE_FINGER_UP | FP_MODE_ANY_CAPTURE);

inline constexpr uint32_t FP_MODE_ANY_WAIT_IRQ =
	(FP_MODE_FINGER_DOWN | FP_MODE_ANY_CAPTURE);

struct FpSensorMode {
	uint32_t raw = 0;

	constexpr FpSensorMode(uint32_t mode = 0) noexcept : raw(mode)
	{
	}

	[[nodiscard]] constexpr bool has_any(uint32_t mask) const noexcept
	{
		return (raw & mask) != 0;
	}

	[[nodiscard]] constexpr fp_capture_type capture_type() const noexcept
	{
		return FP_CAPTURE_TYPE(raw);
	}

	[[nodiscard]] constexpr uint32_t algo_mode() const noexcept
	{
		return raw & ~FP_MODE_CAPTURE_TYPE_MASK;
	}

	[[nodiscard]] constexpr FpSensorMode
	newly_enabled_flags(FpSensorMode previous) const noexcept
	{
		return FpSensorMode(raw & (previous.raw ^ raw));
	}

	[[nodiscard]] constexpr int count(uint32_t mask) const noexcept
	{
		return std::popcount(raw & mask);
	}

	[[nodiscard]] static constexpr uint32_t
	encode_capture_type(fp_capture_type type) noexcept
	{
		return (static_cast<uint32_t>(type)
			<< FP_MODE_CAPTURE_TYPE_SHIFT) &
		       FP_MODE_CAPTURE_TYPE_MASK;
	}
};

[[nodiscard]] constexpr uint32_t make_fp_enroll_event(int error_code,
						      int percent = 0) noexcept
{
	return EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERRCODE(error_code) |
	       (static_cast<uint32_t>(percent)
		<< EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
}

[[nodiscard]] constexpr uint32_t
make_fp_match_event(int error_code, int32_t finger_idx) noexcept
{
	return EC_MKBP_FP_MATCH | EC_MKBP_FP_ERRCODE(error_code) |
	       ((static_cast<uint32_t>(finger_idx)
		 << EC_MKBP_FP_MATCH_IDX_OFFSET) &
		EC_MKBP_FP_MATCH_IDX_MASK);
}

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_ */
