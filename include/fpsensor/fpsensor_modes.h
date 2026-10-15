/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_

#include "ec_commands.h"

#include <bit>
#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>

struct fpsensor_context;

/**
 * Validated fingerprint capture type in [0, FP_CAPTURE_TYPE_MAX).
 */
class FpCaptureType {
    public:
	constexpr FpCaptureType() noexcept = default;

	static constexpr std::optional<FpCaptureType>
	create(std::integral auto type) noexcept
	{
		if (std::cmp_less(type, 0) ||
		    std::cmp_greater_equal(
			    type, static_cast<int>(FP_CAPTURE_TYPE_MAX))) {
			return std::nullopt;
		}
		return FpCaptureType(static_cast<fp_capture_type>(type));
	}

	static constexpr std::optional<FpCaptureType>
	create(fp_capture_type type) noexcept
	{
		return create(static_cast<int>(type));
	}

	[[nodiscard]] constexpr fp_capture_type raw() const noexcept
	{
		return type_;
	}

	constexpr operator fp_capture_type() const noexcept
	{
		return type_;
	}

	[[nodiscard]] constexpr uint32_t to_mode_bits() const noexcept
	{
		return (static_cast<uint32_t>(type_)
			<< FP_MODE_CAPTURE_TYPE_SHIFT) &
		       FP_MODE_CAPTURE_TYPE_MASK;
	}

	constexpr bool
	operator==(const FpCaptureType &) const noexcept = default;

    private:
	friend class FpSensorMode;

	explicit constexpr FpCaptureType(fp_capture_type type) noexcept
		: type_(type)
	{
	}

	fp_capture_type type_ = FP_CAPTURE_VENDOR_FORMAT;
};

inline constexpr uint32_t FP_MODE_ANY_CAPTURE =
	(FP_MODE_CAPTURE | FP_MODE_ENROLL_IMAGE | FP_MODE_MATCH);

inline constexpr uint32_t FP_MODE_ANY_DETECT_FINGER =
	(FP_MODE_FINGER_DOWN | FP_MODE_FINGER_UP | FP_MODE_ANY_CAPTURE);

inline constexpr uint32_t FP_MODE_ANY_WAIT_IRQ =
	(FP_MODE_FINGER_DOWN | FP_MODE_ANY_CAPTURE);

class FpSensorMode {
    public:
	constexpr FpSensorMode() noexcept = default;

	/**
	 * Validate intrinsic mode bitmask invariants (valid capture type,
	 * valid mode bits, and ENROLL_IMAGE accompanied by ENROLL_SESSION).
	 */
	[[nodiscard]] static constexpr
		std::expected<FpSensorMode, ec_error_list>
		create(uint32_t mode) noexcept
	{
		if (!FpCaptureType::create(FP_CAPTURE_TYPE(mode)).has_value()) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		const FpSensorMode candidate(mode);
		if (candidate.algo_mode() & ~FP_VALID_MODES) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		if (candidate.has_any(FP_MODE_ENROLL_IMAGE) &&
		    !candidate.has_any(FP_MODE_ENROLL_SESSION)) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		return candidate;
	}

	/**
	 * Validate both intrinsic mode invariants and contextual state
	 * transition invariants against |ctx|.
	 */
	[[nodiscard]] static std::expected<FpSensorMode, ec_error_list>
	create(uint32_t mode, const fpsensor_context &ctx);

	[[nodiscard]] constexpr uint32_t raw() const noexcept
	{
		return raw_;
	}

	[[nodiscard]] constexpr bool has_any(uint32_t mask) const noexcept
	{
		return (raw_ & mask) != 0;
	}

	[[nodiscard]] constexpr FpCaptureType capture_type() const noexcept
	{
		return FpCaptureType(FP_CAPTURE_TYPE(raw_));
	}

	[[nodiscard]] constexpr uint32_t algo_mode() const noexcept
	{
		return raw_ & ~FP_MODE_CAPTURE_TYPE_MASK;
	}

	[[nodiscard]] constexpr bool
	enables_any(uint32_t mask, uint32_t previous_raw) const noexcept
	{
		return ((raw_ & ~previous_raw) & mask) != 0;
	}

	[[nodiscard]] constexpr int count(uint32_t mask) const noexcept
	{
		return std::popcount(raw_ & mask);
	}

	[[nodiscard]] static constexpr uint32_t
	encode_capture_type(fp_capture_type type) noexcept
	{
		return (static_cast<uint32_t>(type)
			<< FP_MODE_CAPTURE_TYPE_SHIFT) &
		       FP_MODE_CAPTURE_TYPE_MASK;
	}

    private:
	friend class FpSensorStateMachine;
	friend std::expected<FpSensorMode, ec_status> fp_set_sensor_mode(
		uint32_t mode,
		std::optional<std::span<const uint8_t, FP_MAC_LENGTH> > mac);

	explicit constexpr FpSensorMode(uint32_t mode) noexcept : raw_(mode)
	{
	}

	uint32_t raw_ = 0;
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
