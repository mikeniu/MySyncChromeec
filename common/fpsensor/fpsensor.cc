/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_btn_ign_out.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_detect.h"
#include "fpsensor/fpsensor_led.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_platform.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_state_machine.h"
#include "fpsensor/fpsensor_utils.h"
#include "mkbp_event.h"
#include "scoped_fast_cpu.h"
#include "task.h"
#include "util.h"

#include <chrono>
#include <optional>
#include <span>
#include <variant>

#if !defined(CONFIG_RNG)
#error "fpsensor requires RNG"
#endif

#if defined(CONFIG_CROS_EC_RO)
#error "fpsensor code should not be in RO image."
#endif

void FpSensorStateMachine::send_mkbp_event(FpMkbpEvent event)
{
	ctx_.events.add(event);
	mkbp_send_event(EC_MKBP_EVENT_FINGERPRINT);
}

FpSensorStateMachine::Command
FpSensorStateMachine::parse_command(FpSensorMode mode) noexcept
{
	if (!mode.is_finger_needed()) {
		return ImmediateCaptureCommand{ mode.capture_type() };
	}

	if (mode.is_detect_finger()) {
		return DetectFingerCommand{
			.state =
				DetectFingerState{
					.notify_finger_down =
						mode.is_finger_down(),
					.notify_finger_up = mode.is_finger_up(),
					.deepsleep = mode.is_deepsleep(),
					.capture =
						CaptureRequest::from_mode(mode),
				},
		};
	}

	if (mode.is_reset_sensor()) {
		return ResetSensorCommand{};
	}
	if (mode.is_maintenance()) {
		return MaintenanceCommand{};
	}
	if (mode.is_encrypt_template()) {
		return EncryptTemplateCommand{};
	}
	if (mode.is_decrypt_template()) {
		return DecryptTemplateCommand{};
	}

	return IdleCommand{
		.deepsleep = mode.is_deepsleep(),
	};
}

const FpSensorStateMachine::State &FpSensorStateMachine::state() const noexcept
{
	return state_;
}

std::optional<std::chrono::microseconds>
FpSensorStateMachine::timeout() const noexcept
{
	const auto *detect = std::get_if<DetectFingerState>(&state_);
	if (detect != nullptr && detect->notify_finger_up) {
		return kFingerPollingDelay;
	}
	return std::nullopt;
}

FpSensorMode FpSensorStateMachine::to_sensor_mode() const noexcept
{
	uint32_t raw = std::visit(
		[](const auto &s) { return s.to_mode_bits(); }, state_);

#ifdef HAVE_FP_PRIVATE_DRIVER
	raw |= enroll_session_.to_mode_bits();
#endif
	return FpSensorMode(raw);
}

void FpSensorStateMachine::init()
{
	state_ = IdleState{};
#ifdef HAVE_FP_PRIVATE_DRIVER
	enroll_session_.reset();
#endif

	/* Lock PM for initialization. */
	fp_platform::lock_pm();
	CPRINTS("FP_SENSOR_SEL: %s", FpDetectedHardware::sensor_type_name());

#ifdef HAVE_FP_PRIVATE_DRIVER
	/* Reset and initialize the sensor IC */
	fp_sensor_init();

	ctx_.frame.init_cache();
#endif
}

void FpSensorStateMachine::run()
{
	init();

	while (true) {
		FpTaskEvent evt;
		{
#ifdef HAVE_FP_PRIVATE_DRIVER
			fp_platform::ScopedPmUnlock pm_unlock(
				!enroll_session_.is_active());
#endif
			/* Wait for a sensor IRQ or a new mode configuration */
			evt = FpTaskEvent(task_wait_event(static_cast<int>(
				timeout()
					.transform(
						&std::chrono::microseconds::count)
					.value_or(-1))));
		}

		handle_event(evt);
	}
}

#ifdef HAVE_FP_PRIVATE_DRIVER

void FpEnrollSession::sync(bool requested)
{
	if (requested != active_) {
		if (requested) {
			active_ = (fp_enrollment_begin() == 0);
		} else {
			abort();
		}
	}
}

void FpEnrollSession::abort()
{
	active_ = false;
	fp_enrollment_finish(nullptr);
}

int FpEnrollSession::finish(std::span<uint8_t, FP_ALGORITHM_TEMPLATE_SIZE> templ)
{
	active_ = false;
	return fp_enrollment_finish(templ.data());
}

FpMkbpEvent FpSensorStateMachine::process_capture(CaptureOnly)
{
	return FpMkbpEvent::image_ready();
}

FpMkbpEvent FpSensorStateMachine::process_capture(EnrollImage)
{
	int percent = 0;

	/* Prevent enrollment if we have reached max capacity. */
	const auto enroll_id = ctx_.templates.next_free_id();
	if (!enroll_id.has_value()) {
		CPRINTS("Error: Max templates reached.");
		enroll_session_.abort();
		return FpMkbpEvent::enroll(EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	}

	if (ctx_.templates.template_newly_enrolled.has_value()) {
		CPRINTS("Warning: previously enrolled template has not been "
			"read yet.");
	}

	/* begin/continue enrollment */
	CPRINTS("[%d]Enrolling ...", enroll_id->raw());
	int res = fp_finger_enroll(ctx_.frame.image_buffer.data(), &percent);
	CPRINTS("[%d]Enroll =>%d (%d%%)", enroll_id->raw(), res, percent);
	if (res < 0) {
		return FpMkbpEvent::enroll(EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	}
	ctx_.templates.mark_dirty(*enroll_id);
	if (percent == 100) {
		res = enroll_session_.finish(
			ctx_.templates.templates[enroll_id->raw()]);
		if (res != 0) {
			res = EC_MKBP_FP_ERR_ENROLL_INTERNAL;
		} else {
			ctx_.templates.finish_enrollment(
				*enroll_id, ctx_.crypto.auth_enabled());
		}
	}
	return FpMkbpEvent::enroll(res, percent);
}

FpMkbpEvent FpSensorStateMachine::process_capture(MatchFinger match)
{
	const FpTimestamp t0 = FpClock::now();
	const FpMatchResult result = ctx_.templates.match(
		ctx_.frame.image_buffer, match.allow_template_update);

	fp_led::update_match(result.event.is_match_success());
	ctx_.stats.record_match(t0, result.matched_id, result.valid);
	return result.event;
}

bool FpSensorStateMachine::process_finger_capture(const CaptureRequest &capture)
{
	const FpTimestamp t0 = FpClock::now();
	ctx_.frame.begin_capture();

	CPRINTS("Capturing ...");
	const int res = fp_acquire_image(ctx_.frame.image_buffer.data(),
					 capture.capture_type.raw());
	ctx_.stats.record_capture(t0, res == 0);
	if (res != 0) {
		return false;
	}

	ctx_.frame.finish_capture(capture.capture_type);

	fp_platform::flush_spi_after_acquire();

	/* we need CPU power to do the computations */
	ScopedFastCpu fast_cpu;

	const FpMkbpEvent evt =
		std::visit([this](const auto &k) { return process_capture(k); },
			   capture.kind);

	ctx_.stats.finish_overall();
	send_mkbp_event(evt);
	return true;
}

FpSensorStateMachine::State
FpSensorStateMachine::execute_command(IdleCommand cmd)
{
	fp_platform::enter_sensor_low_power();
	return IdleState{ .deepsleep = cmd.deepsleep };
}

FpSensorStateMachine::State
FpSensorStateMachine::execute_command(ImmediateCaptureCommand cmd)
{
	ctx_.frame.begin_capture();
	if (fp_acquire_image(ctx_.frame.image_buffer.data(),
			     cmd.capture_type.raw()) == 0) {
		ctx_.frame.finish_capture(cmd.capture_type);
	}
	send_mkbp_event(FpMkbpEvent::image_ready());
	return IdleState{};
}

FpSensorStateMachine::State
FpSensorStateMachine::execute_command(DetectFingerCommand cmd)
{
	fp_configure_detect();
	if (cmd.state.deepsleep) {
		fp_platform::enter_sensor_low_power();
	}
	if (cmd.state.waiting_for_irq()) {
		fp_platform::enable_sensor_irq();
	} else {
		fp_platform::enter_sensor_low_power();
	}
	return cmd.state;
}

FpSensorStateMachine::State
FpSensorStateMachine::execute_command(ResetSensorCommand)
{
	ctx_.reset_and_clear_context();
	return IdleState{};
}

FpSensorStateMachine::State
FpSensorStateMachine::execute_command(MaintenanceCommand)
{
	fp_maintenance();
	return IdleState{};
}

FpSensorStateMachine::State
FpSensorStateMachine::execute_command(EncryptTemplateCommand)
{
	ctx_.encrypt_requested_template();
	return IdleState{};
}

FpSensorStateMachine::State
FpSensorStateMachine::execute_command(DecryptTemplateCommand)
{
	ctx_.decrypt_and_commit_template();
	return IdleState{};
}

void FpSensorStateMachine::handle_mode_change()
{
	fp_platform::prepare_for_mode_change();

	const FpSensorMode requested_mode = ctx_.sensor_mode;
	enroll_session_.sync(requested_mode.is_enroll_session());

	const Command cmd = parse_command(requested_mode);
	state_ = std::visit(
		[this](const auto &c) { return execute_command(c); }, cmd);
	const FpSensorMode active_mode = to_sensor_mode();
	ctx_.sensor_mode = active_mode;

	if (!std::holds_alternative<ImmediateCaptureCommand>(cmd)) {
		fp_btn_ign_out::update(active_mode);
		fp_led::update_mode(active_mode);
	}
}

bool FpSensorStateMachine::waiting_for_irq() const noexcept
{
	const auto *detect = std::get_if<DetectFingerState>(&state_);
	return detect != nullptr && detect->waiting_for_irq();
}

FpSensorStateMachine::State
FpSensorStateMachine::transition_on_sensor_or_timer(IdleState state)
{
	return state;
}

FpSensorStateMachine::State
FpSensorStateMachine::transition_on_sensor_or_timer(DetectFingerState state)
{
	const finger_state st = fp_finger_status();
	if (st == FINGER_PRESENT && state.notify_finger_down) {
		CPRINTS("Finger!");
		state.notify_finger_down = false;
		send_mkbp_event(FpMkbpEvent::finger_down());
	}
	if (st == FINGER_NONE && state.notify_finger_up) {
		state.notify_finger_up = false;
		send_mkbp_event(FpMkbpEvent::finger_up());
	}
	if (st == FINGER_PRESENT && state.capture.has_value()) {
		CPRINTS("Finger down (capture mode)!");
		/*
		 * Send FINGER_DOWN immediately upon detection
		 * during a capture session so the host can
		 * accurately measure interaction latency.
		 */
		send_mkbp_event(FpMkbpEvent::finger_down());
		if (process_finger_capture(*state.capture)) {
			state.capture.reset();
		}
	}

	if (state.waiting_for_irq() || state.notify_finger_up) {
		return state;
	}
	return IdleState{ .deepsleep = state.deepsleep };
}

void FpSensorStateMachine::handle_sensor_or_timer_event(FpTaskEvent evt)
{
	ctx_.stats.start_overall();
	fp_platform::prepare_for_sensor_or_timer_event(evt);

	state_ = std::visit(
		[this](auto s) -> State {
			return transition_on_sensor_or_timer(s);
		},
		state_);
	if (waiting_for_irq()) {
		fp_configure_detect();
		fp_platform::enable_sensor_irq();
	} else {
		fp_platform::enter_sensor_low_power(evt);
	}

	const FpSensorMode active_mode = to_sensor_mode();
	ctx_.sensor_mode = active_mode;

	fp_btn_ign_out::update(active_mode);
	fp_led::update_mode(active_mode);
}

#endif /* HAVE_FP_PRIVATE_DRIVER */

void FpSensorStateMachine::handle_event(FpTaskEvent evt)
{
#ifdef HAVE_FP_PRIVATE_DRIVER
	if (evt.is_update_config()) {
		handle_mode_change();
	} else if (evt.is_sensor_or_timer()) {
		handle_sensor_or_timer_event(evt);
	}
#else
	send_mkbp_event(FpMkbpEvent{ evt.raw() });
#endif
}

/*
 * Only the RTOS task entry point below is permitted to reference
 * global_context. All C++ domain methods above must operate on their own
 * `this` instance.
 */
extern FpSensorContext global_context;

extern "C" void fp_task()
{
	FpSensorStateMachine{ global_context }.run();
}
