/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor task state machine */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_platform.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_types.h"
#include "fpsensor/fpsensor_utils.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

#ifdef HAVE_FP_PRIVATE_DRIVER
/**
 * Manages the active fingerprint algorithm enrollment session lifecycle
 * (fp_enrollment_begin() / fp_enrollment_finish()).
 */
class FpEnrollSession {
    public:
	constexpr void reset() noexcept
	{
		active_ = false;
	}

	[[nodiscard]] constexpr bool is_active() const noexcept
	{
		return active_;
	}

	[[nodiscard]] constexpr uint32_t to_mode_bits() const noexcept
	{
		return active_ ? FP_MODE_ENROLL_SESSION : 0;
	}

	void sync(bool requested);
	void abort();
	[[nodiscard]] int
	finish(std::span<uint8_t, FP_ALGORITHM_TEMPLATE_SIZE> templ);

    private:
	bool active_ = false;
};
#endif /* HAVE_FP_PRIVATE_DRIVER */

class FpSensorStateMachine {
    public:
	static constexpr std::chrono::microseconds kFingerPollingDelay =
		std::chrono::milliseconds(100);

	struct CaptureOnly {
		[[nodiscard]] constexpr uint32_t to_mode_bits() const noexcept
		{
			return FP_MODE_CAPTURE;
		}
	};
	struct EnrollImage {
		[[nodiscard]] constexpr uint32_t to_mode_bits() const noexcept
		{
			return FP_MODE_ENROLL_IMAGE;
		}
	};
	struct MatchFinger {
		bool allow_template_update = true;

		[[nodiscard]] constexpr uint32_t to_mode_bits() const noexcept
		{
			return FP_MODE_MATCH |
			       (allow_template_update ?
					0 :
					FP_MODE_MATCH_NO_TEMPLATE_UPDATE);
		}
	};
	using CaptureKind = std::variant<CaptureOnly, EnrollImage, MatchFinger>;

	struct CaptureRequest {
		FpCaptureType capture_type{};
		CaptureKind kind = CaptureOnly{};

		[[nodiscard]] static constexpr std::optional<CaptureRequest>
		from_mode(FpSensorMode mode) noexcept
		{
			const FpCaptureType capture_type = mode.capture_type();
			if (mode.is_enroll_image()) {
				return CaptureRequest{
					.capture_type = capture_type,
					.kind = EnrollImage{},
				};
			}
			if (mode.is_match()) {
				return CaptureRequest{
					.capture_type = capture_type,
					.kind =
						MatchFinger{
							.allow_template_update =
								!mode.is_match_no_template_update(),
						},
				};
			}
			if (mode.is_capture()) {
				return CaptureRequest{
					.capture_type = capture_type,
					.kind = CaptureOnly{},
				};
			}
			return std::nullopt;
		}

		[[nodiscard]] constexpr uint32_t to_mode_bits() const noexcept
		{
			return capture_type.to_mode_bits() |
			       std::visit(
				       [](const auto &k) {
					       return k.to_mode_bits();
				       },
				       kind);
		}
	};

	/*
	 * Resting states of the state machine while waiting for an event.
	 */
	struct IdleState {
		bool deepsleep = false;

		[[nodiscard]] constexpr uint32_t to_mode_bits() const noexcept
		{
			return deepsleep ? FP_MODE_DEEPSLEEP : 0;
		}
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

		[[nodiscard]] constexpr uint32_t to_mode_bits() const noexcept
		{
			return (deepsleep ? FP_MODE_DEEPSLEEP : 0) |
			       (notify_finger_down ? FP_MODE_FINGER_DOWN : 0) |
			       (notify_finger_up ? FP_MODE_FINGER_UP : 0) |
			       capture.transform(&CaptureRequest::to_mode_bits)
				       .value_or(0);
		}
	};

	using State = std::variant<IdleState, DetectFingerState>;

	/*
	 * Commands decoded from ctx_.sensor_mode on TASK_EVENT_UPDATE_CONFIG.
	 */
	struct IdleCommand {
		bool deepsleep = false;
	};
	struct ImmediateCaptureCommand {
		FpCaptureType capture_type{};
	};
	struct DetectFingerCommand {
		DetectFingerState state;
	};
	struct ResetSensorCommand {};
	struct MaintenanceCommand {};
	struct EncryptTemplateCommand {};
	struct DecryptTemplateCommand {};

	using Command = std::variant<IdleCommand, ImmediateCaptureCommand,
				     DetectFingerCommand, ResetSensorCommand,
				     MaintenanceCommand, EncryptTemplateCommand,
				     DecryptTemplateCommand>;

	explicit constexpr FpSensorStateMachine(FpSensorContext &ctx) noexcept
		: ctx_(ctx)
	{
	}

	[[nodiscard]] static Command parse_command(FpSensorMode mode) noexcept;

	void run();
	void handle_event(FpTaskEvent evt);
	[[nodiscard]] const State &state() const noexcept;
	[[nodiscard]] std::optional<std::chrono::microseconds>
	timeout() const noexcept;
	[[nodiscard]] FpSensorMode to_sensor_mode() const noexcept;

    private:
	void init();
	void send_mkbp_event(FpMkbpEvent event);
#ifdef HAVE_FP_PRIVATE_DRIVER
	[[nodiscard]] bool waiting_for_irq() const noexcept;
	void handle_mode_change();
	void handle_sensor_or_timer_event(FpTaskEvent evt);
	State execute_command(IdleCommand cmd);
	State execute_command(ImmediateCaptureCommand cmd);
	State execute_command(DetectFingerCommand cmd);
	State execute_command(ResetSensorCommand cmd);
	State execute_command(MaintenanceCommand cmd);
	State execute_command(EncryptTemplateCommand cmd);
	State execute_command(DecryptTemplateCommand cmd);
	State transition_on_sensor_or_timer(IdleState state);
	State transition_on_sensor_or_timer(DetectFingerState state);
	bool process_finger_capture(const CaptureRequest &capture);
	FpMkbpEvent process_capture(CaptureOnly);
	FpMkbpEvent process_capture(EnrollImage);
	FpMkbpEvent process_capture(MatchFinger match);
#endif /* HAVE_FP_PRIVATE_DRIVER */

	FpSensorContext &ctx_;
	State state_ = IdleState{};
#ifdef HAVE_FP_PRIVATE_DRIVER
	FpEnrollSession enroll_session_{};
#endif
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_ */
