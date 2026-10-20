/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_

#include "ec_commands.h"

#include <concepts>
#include <cstdint>
#include <expected>
#include <utility>

class FpCaptureType {
    public:
	constexpr FpCaptureType() noexcept = default;

	static constexpr std::expected<FpCaptureType, ec_error_list>
	create(std::integral auto type) noexcept
	{
		if (std::cmp_less(type, 0) ||
		    std::cmp_greater_equal(
			    type, std::to_underlying(FP_CAPTURE_TYPE_MAX))) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		return FpCaptureType(static_cast<fp_capture_type>(type));
	}

	static constexpr std::expected<FpCaptureType, ec_error_list>
	create(fp_capture_type type) noexcept
	{
		return create(std::to_underlying(type));
	}

	[[nodiscard]] constexpr fp_capture_type raw() const noexcept
	{
		return type_;
	}

	[[nodiscard]] constexpr uint32_t to_mode_bits() const noexcept
	{
		return (static_cast<uint32_t>(std::to_underlying(type_))
			<< FP_MODE_CAPTURE_TYPE_SHIFT) &
		       FP_MODE_CAPTURE_TYPE_MASK;
	}

	[[nodiscard]] constexpr bool is_test_capture() const noexcept
	{
		return type_ == FP_CAPTURE_DEFECT_PXL_TEST ||
		       type_ == FP_CAPTURE_ABNORMAL_TEST ||
		       type_ == FP_CAPTURE_NOISE_TEST ||
		       type_ == FP_CAPTURE_PATTERN0 ||
		       type_ == FP_CAPTURE_PATTERN1 ||
		       type_ == FP_CAPTURE_RESET_TEST ||
		       type_ == FP_CAPTURE_PATTERN2;
	}

	[[nodiscard]] constexpr bool skip_image_offset() const noexcept
	{
		return !(type_ == FP_CAPTURE_VENDOR_FORMAT ||
			 type_ == FP_CAPTURE_QUALITY_TEST);
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
	explicit constexpr FpSensorMode(uint32_t mode) noexcept : raw_(mode)
	{
	}

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
		    !candidate.is_enroll_session()) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		return candidate;
	}

	[[nodiscard]] constexpr uint32_t raw() const noexcept
	{
		return raw_;
	}
	[[nodiscard]] constexpr bool has_any(uint32_t mask) const noexcept
	{
		return (raw_ & mask) != 0;
	}
	[[nodiscard]] constexpr bool is_enroll_session() const noexcept
	{
		return has_any(FP_MODE_ENROLL_SESSION);
	}
	[[nodiscard]] constexpr bool is_match() const noexcept
	{
		return has_any(FP_MODE_MATCH);
	}
	[[nodiscard]] constexpr bool is_detect_finger() const noexcept
	{
		return has_any(FP_MODE_ANY_DETECT_FINGER);
	}
	[[nodiscard]] constexpr FpCaptureType capture_type() const noexcept
	{
		return FpCaptureType(FP_CAPTURE_TYPE(raw_));
	}
	[[nodiscard]] constexpr uint32_t algo_mode() const noexcept
	{
		return raw_ & ~FP_MODE_CAPTURE_TYPE_MASK;
	}
	[[nodiscard]] constexpr bool is_finger_needed() const noexcept
	{
		return !(has_any(FP_MODE_CAPTURE) &&
			 capture_type().is_test_capture());
	}

	constexpr bool
	operator==(const FpSensorMode &) const noexcept = default;

    private:
	uint32_t raw_ = 0;
};

[[nodiscard]] inline constexpr bool is_finger_needed(uint32_t mode) noexcept
{
	return FpSensorMode::create(mode)
		.transform(&FpSensorMode::is_finger_needed)
		.value_or(true);
}

[[nodiscard]] inline constexpr bool
skip_image_offset(enum fp_capture_type type) noexcept
{
	return FpCaptureType::create(type)
		.transform(&FpCaptureType::skip_image_offset)
		.value_or(true);
}

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_ */
