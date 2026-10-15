/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor task state machine */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_types.h"
#include "timer.h"

#include <cstdint>
#include <optional>
#include <span>
#include <variant>

struct FpSensorStats {
	uint32_t capture_time_us{};
	uint32_t matching_time_us{};
	uint32_t overall_time_us{};
	timestamp_t overall_t0{};
	uint8_t timestamps_invalid{};
	/*
	 * Last matched template index, persisted for telemetry
	 * (EC_CMD_FP_STATS). Unlike global_context, this is not cleared when
	 * the secret is read.
	 */
	int8_t template_matched{};
};

class FpSensorStateMachine {
    public:
	static constexpr int kWaitForeverUs = -1;
	static constexpr int kFingerPollingDelayUs = 100 * MSEC;

	struct CaptureOnly {};
	struct EnrollImage {};
	struct MatchFinger {
		bool allow_template_update = true;
	};
	using CaptureKind = std::variant<CaptureOnly, EnrollImage, MatchFinger>;

	struct CaptureRequest {
		fp_capture_type capture_type = FP_CAPTURE_VENDOR_FORMAT;
		CaptureKind kind = CaptureOnly{};
	};

	struct IdleState {};
	struct ImmediateCaptureState {
		fp_capture_type capture_type = FP_CAPTURE_VENDOR_FORMAT;
	};
	struct DetectFingerState {
		bool notify_finger_down = false;
		bool notify_finger_up = false;
		bool deepsleep = false;
		std::optional<CaptureRequest> capture = std::nullopt;

		[[nodiscard]] constexpr bool waiting_for_irq() const noexcept
		{
			return notify_finger_down || capture.has_value();
		}
	};
	struct ResetSensorState {};
	struct MaintenanceState {};
	struct EncryptTemplateState {};
	struct DecryptTemplateState {};

	using State =
		std::variant<IdleState, ImmediateCaptureState, DetectFingerState,
			     ResetSensorState, MaintenanceState,
			     EncryptTemplateState, DecryptTemplateState>;

	explicit constexpr FpSensorStateMachine(
		fpsensor_context &ctx = global_context,
		std::span<uint8_t, FP_SENSOR_IMAGE_SIZE> image_buffer =
			fp_buffer,
		std::span<uint8_t[FP_ALGORITHM_TEMPLATE_SIZE],
			  FP_MAX_FINGER_COUNT>
			templates = fp_template) noexcept
		: ctx_(ctx),
		  image_buffer_(image_buffer),
		  templates_(templates)
	{
	}

	[[nodiscard]] static State parse_state(uint32_t mode) noexcept;

	void run();
	void handle_event(uint32_t evt);
	[[nodiscard]] const State &state() const noexcept;
	[[nodiscard]] const FpSensorStats &stats() const noexcept;

    private:
	void init();
#ifdef HAVE_FP_PRIVATE_DRIVER
	void handle_config_update();
	void handle_sensor_or_timer_event(uint32_t evt);
	bool process_finger_capture(const CaptureRequest &capture);
	uint32_t process_enroll();
	uint32_t process_match(bool allow_template_update);
#endif /* HAVE_FP_PRIVATE_DRIVER */

	fpsensor_context &ctx_;
	std::span<uint8_t, FP_SENSOR_IMAGE_SIZE> image_buffer_;
	std::span<uint8_t[FP_ALGORITHM_TEMPLATE_SIZE], FP_MAX_FINGER_COUNT>
		templates_;
	State state_ = IdleState{};
	FpSensorStats stats_{};
	int timeout_us_ = kWaitForeverUs;
#ifdef HAVE_FP_PRIVATE_DRIVER
	bool enroll_session_active_ = false;
#endif
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_ */
