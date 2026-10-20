/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_MODES_H_

#include "ec_commands.h"
#include "fpsensor/fpsensor_utils.h"

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

struct FpSensorContext;

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

	explicit constexpr FpSensorMode(uint32_t mode) noexcept : raw_(mode)
	{
	}

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
		if (candidate.is_enroll_image() &&
		    !candidate.is_enroll_session()) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		return candidate;
	}

	/**
	 * Validate both intrinsic mode invariants and contextual state
	 * transition invariants against |ctx|.
	 */
	[[nodiscard]] static std::expected<FpSensorMode, ec_error_list>
	create(uint32_t mode, const FpSensorContext &ctx);

	[[nodiscard]] constexpr uint32_t raw() const noexcept
	{
		return raw_;
	}

	[[nodiscard]] constexpr bool has_any(uint32_t mask) const noexcept
	{
		return (raw_ & mask) != 0;
	}

	[[nodiscard]] constexpr bool is_finger_down() const noexcept
	{
		return has_any(FP_MODE_FINGER_DOWN);
	}

	[[nodiscard]] constexpr bool is_finger_up() const noexcept
	{
		return has_any(FP_MODE_FINGER_UP);
	}

	[[nodiscard]] constexpr bool is_deepsleep() const noexcept
	{
		return has_any(FP_MODE_DEEPSLEEP);
	}

	[[nodiscard]] constexpr bool is_capture() const noexcept
	{
		return has_any(FP_MODE_CAPTURE);
	}

	[[nodiscard]] constexpr bool is_enroll_session() const noexcept
	{
		return has_any(FP_MODE_ENROLL_SESSION);
	}

	[[nodiscard]] constexpr bool is_enroll_image() const noexcept
	{
		return has_any(FP_MODE_ENROLL_IMAGE);
	}

	[[nodiscard]] constexpr bool is_match() const noexcept
	{
		return has_any(FP_MODE_MATCH);
	}

	[[nodiscard]] constexpr bool
	is_match_no_template_update() const noexcept
	{
		return has_any(FP_MODE_MATCH_NO_TEMPLATE_UPDATE);
	}

	[[nodiscard]] constexpr bool is_reset_sensor() const noexcept
	{
		return has_any(FP_MODE_RESET_SENSOR);
	}

	[[nodiscard]] constexpr bool is_maintenance() const noexcept
	{
		return has_any(FP_MODE_SENSOR_MAINTENANCE);
	}

	[[nodiscard]] constexpr bool is_encrypt_template() const noexcept
	{
		return has_any(FP_MODE_ENCRYPT_TEMPLATE);
	}

	[[nodiscard]] constexpr bool is_decrypt_template() const noexcept
	{
		return has_any(FP_MODE_DECRYPT_TEMPLATE);
	}

	[[nodiscard]] constexpr bool is_dont_change() const noexcept
	{
		return has_any(FP_MODE_DONT_CHANGE);
	}

	[[nodiscard]] constexpr bool is_detect_finger() const noexcept
	{
		return has_any(FP_MODE_ANY_DETECT_FINGER);
	}

	[[nodiscard]] constexpr bool is_crypto_in_progress() const noexcept
	{
		return has_any(FP_MODES_CRYPTO_IN_PROGRESS);
	}

	[[nodiscard]] constexpr bool is_template_operation() const noexcept
	{
		return has_any(FP_MODES_TEMPLATE_OPERATION);
	}

	[[nodiscard]] constexpr bool is_active() const noexcept
	{
		return has_any(FP_VALID_MODES);
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
		return !(is_capture() && capture_type().is_test_capture());
	}

	/**
	 * Return the operation string ("enroll" or "auth") required to
	 * transition from |previous| to this mode, std::nullopt if no
	 * authentication is required, or EC_ERROR_INVAL if multiple
	 * mutually-exclusive auth-gated modes are enabled simultaneously.
	 */
	[[nodiscard]] constexpr
		std::expected<std::optional<std::string_view>, ec_error_list>
		required_auth_operation(FpSensorMode previous) const noexcept
	{
		if (!enables_any(FP_MODES_WITH_AUTHENTICATION, previous)) {
			return std::nullopt;
		}
		if (count(FP_MODES_WITH_AUTHENTICATION) > 1) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		return enables_any(FP_MODE_ENROLL_SESSION, previous) ?
			       std::optional<std::string_view>{ "enroll" } :
			       std::optional<std::string_view>{ "auth" };
	}

	/**
	 * Authenticate transitioning to this mode against |ctx| using |mac|.
	 */
	[[nodiscard]] std::expected<void, ec_error_list>
	authenticate(FpSensorContext &ctx,
		     std::optional<std::span<const uint8_t, FP_MAC_LENGTH> >
			     mac) const;

	constexpr bool
	operator==(const FpSensorMode &) const noexcept = default;

#if !defined(CONFIG_ZEPHYR) && defined(TEST_BUILD)
	constexpr FpSensorMode &operator=(uint32_t mode) noexcept
	{
		raw_ = mode;
		return *this;
	}

	constexpr operator uint32_t() const noexcept
	{
		return raw_;
	}
#endif

    private:
	uint32_t raw_ = 0;
};

inline constexpr uint16_t kFpNoSuchTemplate =
	std::numeric_limits<uint16_t>::max();

/**
 * Typed wrapper around a 32-bit fingerprint MKBP event word.
 */
struct FpMkbpEvent {
	uint32_t raw = 0;

	[[nodiscard]] static constexpr FpMkbpEvent
	enroll(int error_code, int percent = 0) noexcept
	{
		return FpMkbpEvent{
			EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERRCODE(error_code) |
				(static_cast<uint32_t>(percent)
				 << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET),
		};
	}

	[[nodiscard]] static constexpr FpMkbpEvent
	match(int error_code, int32_t finger_idx = kFpNoSuchTemplate) noexcept
	{
		return FpMkbpEvent{
			EC_MKBP_FP_MATCH | EC_MKBP_FP_ERRCODE(error_code) |
				((static_cast<uint32_t>(finger_idx)
				  << EC_MKBP_FP_MATCH_IDX_OFFSET) &
				 EC_MKBP_FP_MATCH_IDX_MASK),
		};
	}

	[[nodiscard]] static constexpr FpMkbpEvent finger_down() noexcept
	{
		return FpMkbpEvent{ EC_MKBP_FP_FINGER_DOWN };
	}

	[[nodiscard]] static constexpr FpMkbpEvent finger_up() noexcept
	{
		return FpMkbpEvent{ EC_MKBP_FP_FINGER_UP };
	}

	[[nodiscard]] static constexpr FpMkbpEvent image_ready() noexcept
	{
		return FpMkbpEvent{ EC_MKBP_FP_IMAGE_READY };
	}

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

	[[nodiscard]] constexpr bool is_match_success() const noexcept
	{
		return fp_match_success(static_cast<int>(error_code()));
	}

	[[nodiscard]] constexpr int enroll_progress() const noexcept
	{
		return EC_MKBP_FP_ENROLL_PROGRESS(raw);
	}

	[[nodiscard]] constexpr const char *enroll_error_name() const noexcept
	{
		constexpr std::array<const char *, 4> kEnrollErrorNames = {
			"OK", "Low Quality", "Immobile", "Low Coverage"
		};
		return kEnrollErrorNames[error_code() %
					 kEnrollErrorNames.size()];
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
