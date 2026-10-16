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
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_types.h"
#include "timer.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

struct FpSensorStats {
	using Duration = std::chrono::duration<uint32_t, std::micro>;

	Duration capture_time{};
	Duration matching_time{};
	Duration overall_time{};
	timestamp_t overall_t0{};
	uint8_t timestamps_invalid{};
	/*
	 * Last matched template index, persisted for telemetry
	 * (EC_CMD_FP_STATS). Unlike global_context, this is not cleared when
	 * the secret is read.
	 */
	std::optional<FpTemplateId> template_matched = std::nullopt;
};

class FpSensorStateMachine {
    public:
	static constexpr std::chrono::microseconds kFingerPollingDelay =
		std::chrono::milliseconds(100);

	struct CaptureOnly {};
	struct EnrollImage {};
	struct MatchFinger {
		bool allow_template_update = true;
	};
	using CaptureKind = std::variant<CaptureOnly, EnrollImage, MatchFinger>;

	struct CaptureRequest {
		FpCaptureType capture_type{};
		CaptureKind kind = CaptureOnly{};
	};

	/*
	 * Resting states of the state machine while waiting for an event.
	 */
	struct IdleState {
		bool deepsleep = false;
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

	explicit constexpr
	FpSensorStateMachine(fpsensor_context &ctx = global_context) noexcept
		: ctx_(ctx)
	{
	}

	[[nodiscard]] static Command parse_command(FpSensorMode mode) noexcept;

	void run();
	void handle_event(uint32_t evt);
	[[nodiscard]] const State &state() const noexcept;
	[[nodiscard]] const FpSensorStats &stats() const noexcept;
	[[nodiscard]] std::optional<std::chrono::microseconds>
	timeout() const noexcept;
	[[nodiscard]] FpSensorMode to_sensor_mode() const noexcept;

    private:
	void init();
	void send_mkbp_event(uint32_t event);
#ifdef HAVE_FP_PRIVATE_DRIVER
	void abort_enroll_session();
	int finish_enroll_session(
		std::span<uint8_t, FP_ALGORITHM_TEMPLATE_SIZE> templ);
	void handle_config_update();
	void handle_sensor_or_timer_event(uint32_t evt);
	State transition_on_config(const Command &cmd);
	State transition_on_sensor_or_timer(IdleState state, uint32_t evt);
	State transition_on_sensor_or_timer(DetectFingerState state,
					    uint32_t evt);
	bool process_finger_capture(const CaptureRequest &capture);
	uint32_t process_enroll();
	uint32_t process_match(bool allow_template_update);
#endif /* HAVE_FP_PRIVATE_DRIVER */

	fpsensor_context &ctx_;
	State state_ = IdleState{};
	FpSensorStats stats_{};
#ifdef HAVE_FP_PRIVATE_DRIVER
	bool enroll_session_active_ = false;
#endif
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_ */
