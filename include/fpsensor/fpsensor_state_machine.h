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
#include "fpsensor/fpsensor_utils.h"

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
	FpTimestamp overall_t0{};
	bool capture_invalid = false;
	bool matching_invalid = false;
	/*
	 * Last matched template index, persisted for telemetry
	 * (EC_CMD_FP_STATS). Unlike global_context, this is not cleared when
	 * the secret is read.
	 */
	std::optional<FpTemplateId> template_matched = std::nullopt;

	[[nodiscard]] constexpr ec_response_fp_stats to_response() const noexcept
	{
		const uint64_t t0_us = static_cast<uint64_t>(
			overall_t0.time_since_epoch().count());
		const uint8_t timestamps_invalid =
			(capture_invalid ? FPSTATS_CAPTURE_INV : 0) |
			(matching_invalid ? FPSTATS_MATCHING_INV : 0);
		return {
			.capture_time_us = capture_time.count(),
			.matching_time_us = matching_time.count(),
			.overall_time_us = overall_time.count(),
			.overall_t0 = {
				.lo = static_cast<uint32_t>(t0_us),
				.hi = static_cast<uint32_t>(t0_us >> 32),
			},
			.timestamps_invalid = timestamps_invalid,
			.template_matched = static_cast<int8_t>(
				template_matched.transform(&FpTemplateId::raw)
					.value_or(static_cast<uint16_t>(
						FP_NO_SUCH_TEMPLATE))),
		};
	}
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

		[[nodiscard]] static constexpr std::optional<CaptureRequest>
		from_mode(FpSensorMode mode) noexcept
		{
			const FpCaptureType capture_type = mode.capture_type();
			if (mode.has_any(FP_MODE_ENROLL_IMAGE)) {
				return CaptureRequest{
					.capture_type = capture_type,
					.kind = EnrollImage{},
				};
			}
			if (mode.has_any(FP_MODE_MATCH)) {
				return CaptureRequest{
					.capture_type = capture_type,
					.kind =
						MatchFinger{
							.allow_template_update =
								!mode.has_any(
									FP_MODE_MATCH_NO_TEMPLATE_UPDATE),
						},
				};
			}
			if (mode.has_any(FP_MODE_CAPTURE)) {
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
				       Overloaded{
					       [](const CaptureOnly &)
						       -> uint32_t {
						       return FP_MODE_CAPTURE;
					       },
					       [](const EnrollImage &)
						       -> uint32_t {
						       return FP_MODE_ENROLL_IMAGE;
					       },
					       [](const MatchFinger &m)
						       -> uint32_t {
						       return FP_MODE_MATCH |
							      (m.allow_template_update ?
								       0 :
								       FP_MODE_MATCH_NO_TEMPLATE_UPDATE);
					       },
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
