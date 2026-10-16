/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_

#include "ec_commands.h"
#include "fpsensor/fpsensor_utils.h"

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

	constexpr operator fp_capture_type() const noexcept
	{
		return type_;
	}

	[[nodiscard]] constexpr uint32_t to_mode_bits() const noexcept
	{
		return (static_cast<uint32_t>(std::to_underlying(type_))
			<< FP_MODE_CAPTURE_TYPE_SHIFT) &
		       FP_MODE_CAPTURE_TYPE_MASK;
	}

	/**
	 * Return true if this capture type is an internal sensor test pattern
	 * or self-test that does not require a finger on the sensor.
	 */
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

	/**
	 * Return true if this capture type only cares about the embedded/offset
	 * image bytes returned over EC_CMD_FP_FRAME rather than the full frame.
	 */
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
	enables_any(uint32_t mask, FpSensorMode previous) const noexcept
	{
		return ((raw_ & ~previous.raw_) & mask) != 0;
	}

	[[nodiscard]] constexpr int count(uint32_t mask) const noexcept
	{
		return std::popcount(raw_ & mask);
	}

	[[nodiscard]] constexpr bool is_finger_needed() const noexcept
	{
		return !(has_any(FP_MODE_CAPTURE) &&
			 capture_type().is_test_capture());
	}

    private:
	friend struct fpsensor_context;
	friend class FpSensorStateMachine;
	friend std::expected<FpSensorMode, ec_status> fp_set_sensor_mode(
		uint32_t mode,
		std::optional<std::span<const uint8_t, FP_MAC_LENGTH> > mac,
		fpsensor_context &ctx);

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

/**
 * Typed wrapper around a 32-bit fingerprint MKBP event word.
 */
struct FpMkbpEvent {
	uint32_t raw = 0;

	[[nodiscard]] constexpr bool is_enroll() const noexcept
	{
		return (raw & EC_MKBP_FP_ENROLL) != 0;
	}

	[[nodiscard]] constexpr bool is_match() const noexcept
	{
		return (raw & EC_MKBP_FP_MATCH) != 0;
	}

	[[nodiscard]] constexpr bool is_finger_down() const noexcept
	{
		return (raw & EC_MKBP_FP_FINGER_DOWN) != 0;
	}

	[[nodiscard]] constexpr bool is_finger_up() const noexcept
	{
		return (raw & EC_MKBP_FP_FINGER_UP) != 0;
	}

	[[nodiscard]] constexpr bool is_image_ready() const noexcept
	{
		return (raw & EC_MKBP_FP_IMAGE_READY) != 0;
	}

	[[nodiscard]] constexpr uint32_t error_code() const noexcept
	{
		return EC_MKBP_FP_ERRCODE(raw);
	}

	[[nodiscard]] constexpr int enroll_progress() const noexcept
	{
		return EC_MKBP_FP_ENROLL_PROGRESS(raw);
	}

	[[nodiscard]] constexpr int match_index() const noexcept
	{
		return EC_MKBP_FP_MATCH_IDX(raw);
	}
};

/**
 * Unpacked EC_CMD_FP_FRAME v0 offset word (buffer index + byte offset).
 */
struct FpFrameV0Offset {
	uint16_t buffer_index = 0;
	uint32_t offset = 0;

	static constexpr FpFrameV0Offset unpack(uint32_t raw_offset) noexcept
	{
		return {
			.buffer_index = static_cast<uint16_t>(
				FP_FRAME_GET_BUFFER_INDEX(raw_offset)),
			.offset = raw_offset & FP_FRAME_OFFSET_MASK,
		};
	}

	[[nodiscard]] constexpr uint32_t pack() const noexcept
	{
		return (static_cast<uint32_t>(buffer_index)
			<< FP_FRAME_INDEX_SHIFT) |
		       (offset & FP_FRAME_OFFSET_MASK);
	}

	[[nodiscard]] constexpr bool is_raw_image() const noexcept
	{
		return buffer_index == FP_FRAME_INDEX_RAW_IMAGE;
	}

	[[nodiscard]] constexpr int template_index() const noexcept
	{
		return static_cast<int>(buffer_index) - FP_FRAME_INDEX_TEMPLATE;
	}
};

/**
 * Unpacked EC_CMD_FP_TEMPLATE v0 size word (chunk size + commit flag).
 */
struct FpTemplateV0Size {
	uint32_t size = 0;
	bool commit = false;

	static constexpr FpTemplateV0Size unpack(uint32_t raw_size) noexcept
	{
		return {
			.size = raw_size &
				~static_cast<uint32_t>(FP_TEMPLATE_COMMIT),
			.commit = (raw_size & FP_TEMPLATE_COMMIT) != 0,
		};
	}

	[[nodiscard]] constexpr uint32_t pack() const noexcept
	{
		return (size & ~static_cast<uint32_t>(FP_TEMPLATE_COMMIT)) |
		       (commit ? static_cast<uint32_t>(FP_TEMPLATE_COMMIT) : 0);
	}
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_ */
