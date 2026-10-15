/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "assert.h"
#include "atomic.h"
#include "clock.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_btn_ign_out.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_detect.h"
#include "fpsensor/fpsensor_led.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_state_machine.h"
#include "fpsensor/fpsensor_utils.h"
#include "gpio.h"
#include "host_command.h"
#include "link_defs.h"
#include "mkbp_event.h"
#include "openssl/mem.h"
#include "overflow.h"
#include "scoped_fast_cpu.h"
#include "sha256.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "trng.h"
#include "util.h"
#include "watchdog.h"

#include <algorithm>
#include <array>
#include <variant>

#ifdef CONFIG_ZEPHYR
#include <zephyr/pm/policy.h>
#include <zephyr/shell/shell.h>
#endif

#if !defined(CONFIG_RNG)
#error "fpsensor requires RNG"
#endif

#if defined(CONFIG_CROS_EC_RO)
#error "fpsensor code should not be in RO image."
#endif

BUILD_ASSERT(sizeof(struct ec_fp_template_encryption_metadata) % 4 == 0);

static FpSensorStateMachine state_machine;

#ifndef CONFIG_ZEPHYR
/* Define the PM functions for compatibility with EC-legacy. */
static inline void pm_policy_state_all_lock_get(void)
{
}

static inline void pm_policy_state_all_lock_put(void)
{
}
#endif

/* Interrupt line from the fingerprint sensor */
extern "C" void fps_event(enum gpio_signal signal)
{
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_SENSOR_IRQ);
}

static enum ec_error_list encrypt_template(FpTemplateId fgr);
static enum ec_status fp_commit_template(std::span<const uint8_t> context);

namespace
{

template <class... Ts> struct Overloaded : Ts... {
	using Ts::operator()...;
};

} /* namespace */

void FpSensorStateMachine::send_mkbp_event(uint32_t event)
{
	ctx_.add_events(event);
	mkbp_send_event(EC_MKBP_EVENT_FINGERPRINT);
}

FpSensorStateMachine::Command
FpSensorStateMachine::parse_command(FpSensorMode mode) noexcept
{
	const FpCaptureType capture_type = mode.capture_type();

	if (!is_finger_needed(mode.raw())) {
		return ImmediateCaptureCommand{ capture_type };
	}

	if (mode.has_any(FP_MODE_ANY_DETECT_FINGER)) {
		std::optional<CaptureRequest> capture = std::nullopt;
		if (mode.has_any(FP_MODE_ENROLL_IMAGE)) {
			capture = CaptureRequest{
				.capture_type = capture_type,
				.kind = EnrollImage{},
			};
		} else if (mode.has_any(FP_MODE_MATCH)) {
			capture = CaptureRequest{
				.capture_type = capture_type,
				.kind =
					MatchFinger{
						.allow_template_update = !mode.has_any(
							FP_MODE_MATCH_NO_TEMPLATE_UPDATE),
					},
			};
		} else if (mode.has_any(FP_MODE_CAPTURE)) {
			capture = CaptureRequest{
				.capture_type = capture_type,
				.kind = CaptureOnly{},
			};
		}

		return DetectFingerCommand{
			.state =
				DetectFingerState{
					.notify_finger_down = mode.has_any(
						FP_MODE_FINGER_DOWN),
					.notify_finger_up =
						mode.has_any(FP_MODE_FINGER_UP),
					.deepsleep =
						mode.has_any(FP_MODE_DEEPSLEEP),
					.capture = capture,
				},
		};
	}

	if (mode.has_any(FP_MODE_RESET_SENSOR)) {
		return ResetSensorCommand{};
	}
	if (mode.has_any(FP_MODE_SENSOR_MAINTENANCE)) {
		return MaintenanceCommand{};
	}
	if (mode.has_any(FP_MODE_ENCRYPT_TEMPLATE)) {
		return EncryptTemplateCommand{};
	}
	if (mode.has_any(FP_MODE_DECRYPT_TEMPLATE)) {
		return DecryptTemplateCommand{};
	}

	return IdleCommand{
		.deepsleep = mode.has_any(FP_MODE_DEEPSLEEP),
	};
}

const FpSensorStateMachine::State &FpSensorStateMachine::state() const noexcept
{
	return state_;
}

const FpSensorStats &FpSensorStateMachine::stats() const noexcept
{
	return stats_;
}

int FpSensorStateMachine::timeout_us() const noexcept
{
	const auto *detect = std::get_if<DetectFingerState>(&state_);
	if (detect != nullptr && detect->notify_finger_up) {
		return kFingerPollingDelayUs;
	}
	return kWaitForeverUs;
}

FpSensorMode FpSensorStateMachine::to_sensor_mode() const noexcept
{
	uint32_t raw = std::visit(
		Overloaded{
			[](const IdleState &s) -> uint32_t {
				return s.deepsleep ? FP_MODE_DEEPSLEEP : 0;
			},
			[](const DetectFingerState &s) -> uint32_t {
				uint32_t mode = 0;
				if (s.deepsleep) {
					mode |= FP_MODE_DEEPSLEEP;
				}
				if (s.notify_finger_down) {
					mode |= FP_MODE_FINGER_DOWN;
				}
				if (s.notify_finger_up) {
					mode |= FP_MODE_FINGER_UP;
				}
				if (s.capture.has_value()) {
					mode |= FpSensorMode::encode_capture_type(
						s.capture->capture_type);
					mode |= std::visit(
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
						s.capture->kind);
				}
				return mode;
			},
		},
		state_);

#ifdef HAVE_FP_PRIVATE_DRIVER
	if (enroll_session_active_) {
		raw |= FP_MODE_ENROLL_SESSION;
	}
#endif
	return FpSensorMode(raw);
}

void FpSensorStateMachine::init()
{
	state_ = IdleState{};
#ifdef HAVE_FP_PRIVATE_DRIVER
	enroll_session_active_ = false;
#endif

	/* Lock PM for initialization. */
	pm_policy_state_all_lock_get();
	CPRINTS("FP_SENSOR_SEL: %s",
		fp_sensor_type_to_str(fpsensor_detect_get_type()));

#ifdef HAVE_FP_PRIVATE_DRIVER
	/* Reset and initialize the sensor IC */
	fp_sensor_init();

	ctx_.fp_image_frame_params_cache.populate_cache(image_buffer_.size());
#endif
}

void FpSensorStateMachine::run()
{
	init();

	while (true) {
#ifdef HAVE_FP_PRIVATE_DRIVER
		const bool unlock_pm = !enroll_session_active_;
		/*
		 * Unlock PM while waiting for an event except for an
		 * enrollment process.
		 */
		if (unlock_pm) {
			pm_policy_state_all_lock_put();
		}

		/* Wait for a sensor IRQ or a new mode configuration */
		const uint32_t evt = task_wait_event(timeout_us());

		/*
		 * Lock PM for any FP related actions, especially communication
		 * with a FP sensor.
		 */
		if (unlock_pm) {
			pm_policy_state_all_lock_get();
		}
#else
		const uint32_t evt = task_wait_event(timeout_us());
#endif

		handle_event(evt);
	}
}

#ifdef HAVE_FP_PRIVATE_DRIVER

namespace
{

void enable_sensor_irq()
{
#ifndef CONFIG_ZEPHYR
	gpio_clear_pending_interrupt(GPIO_FPS_INT);
	gpio_enable_interrupt(GPIO_FPS_INT);
#endif
}

void enter_sensor_low_power([[maybe_unused]] uint32_t evt = 0)
{
#ifndef CONFIG_ZEPHYR
	if (evt & TASK_EVENT_SENSOR_IRQ) {
		gpio_clear_pending_interrupt(GPIO_FPS_INT);
	}
#endif
	fp_sensor_low_power();
}

void prepare_for_mode_change()
{
	/*
	 * TODO(b/316859625): Remove CONFIG_ZEPHYR block after
	 * migration to Zephyr is completed.
	 */
#ifdef CONFIG_ZEPHYR
	/*
	 * We are about to change sensor mode, so exit any
	 * previous states.
	 */
	fp_idle();
#else
	gpio_disable_interrupt(GPIO_FPS_INT);
#endif
}

void prepare_for_sensor_or_timer_event([[maybe_unused]] uint32_t evt)
{
	/*
	 * TODO(b/316859625): Remove CONFIG_ZEPHYR block after
	 * migration to Zephyr is completed.
	 */
#ifdef CONFIG_ZEPHYR
	/* On timeout, put sensor into idle state. */
	if (evt & TASK_EVENT_TIMER) {
		fp_idle();
	}
#else
	gpio_disable_interrupt(GPIO_FPS_INT);
#endif
}

} /* namespace */

void FpSensorStateMachine::abort_enroll_session()
{
	enroll_session_active_ = false;
	fp_enrollment_finish(nullptr);
}

int FpSensorStateMachine::finish_enroll_session(
	std::span<uint8_t, FP_ALGORITHM_TEMPLATE_SIZE> templ)
{
	enroll_session_active_ = false;
	return fp_enrollment_finish(templ.data());
}

uint32_t FpSensorStateMachine::process_enroll()
{
	int percent = 0;

	/* Prevent enrollment if we have reached max capacity. */
	const auto enroll_id = FpTemplateId::create(ctx_.templ_valid);
	if (!enroll_id.has_value()) {
		CPRINTS("Error: Max templates reached.");
		abort_enroll_session();
		return make_fp_enroll_event(EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	}

	if (ctx_.template_newly_enrolled.has_value()) {
		CPRINTS("Warning: previously enrolled template has not been "
			"read yet.");
	}

	/* begin/continue enrollment */
	CPRINTS("[%d]Enrolling ...", enroll_id->raw());
	int res = fp_finger_enroll(image_buffer_.data(), &percent);
	CPRINTS("[%d]Enroll =>%d (%d%%)", enroll_id->raw(), res, percent);
	if (res < 0) {
		return make_fp_enroll_event(EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	}
	ctx_.mark_template_dirty(*enroll_id);
	if (percent == 100) {
		res = finish_enroll_session(templates_[enroll_id->raw()]);
		if (res != 0) {
			res = EC_MKBP_FP_ERR_ENROLL_INTERNAL;
		} else {
			ctx_.template_newly_enrolled = *enroll_id;
			ctx_.positive_match_secret_state.enable(*enroll_id);

			/*
			 * In the classic flow we immediately use this template
			 * for matching (templ_valid is incremented).
			 *
			 * In the Fingerprint Auth flow we must reliably inform
			 * Trusted Application that the template was enrolled.
			 * To achieve this, we will not use this template for
			 * matching, until Trusted Application sends us a signed
			 * confirmation.
			 */
			if (!fingerprint_auth_enabled()) {
				ctx_.templ_valid++;
			}
		}
	}
	return make_fp_enroll_event(res, percent);
}

uint32_t FpSensorStateMachine::process_match(bool allow_template_update)
{
	const timestamp_t t0 = get_time();
	int res = -1;
	uint32_t updated = 0;
	int32_t fgr = FP_NO_SUCH_TEMPLATE;
	stats_.timestamps_invalid = 0;
	stats_.template_matched.reset();

	/* match finger against current templates */
	ctx_.positive_match_secret_state.disable();

	CPRINTS("Matching/%d ...", ctx_.templ_valid);
	if (ctx_.templ_valid > 0) {
		res = fp_finger_match(templates_[0].data(), ctx_.templ_valid,
				      image_buffer_.data(),
				      allow_template_update, &fgr, &updated);
		CPRINTS("Match =>%d (finger %d)", res, fgr);

		if (fp_match_success(res)) {
			/*
			 * Match succeded! Let's check if template number
			 * is valid. If it is not valid, overwrite result
			 * with EC_MKBP_FP_ERR_MATCH_NO_INTERNAL.
			 */
			if (const auto match_id = FpTemplateId::create(fgr);
			    match_id.has_value()) {
				stats_.template_matched = *match_id;
				ctx_.positive_match_secret_state.enable(
					*match_id);
			} else {
				res = EC_MKBP_FP_ERR_MATCH_NO_INTERNAL;
				stats_.timestamps_invalid |=
					FPSTATS_MATCHING_INV;
			}
		} else if (res < 0) {
			/*
			 * Negative result means that there is a problem with
			 * code responsible for matching. Overwrite it with
			 * MATCH_NO_INTERNAL to let upper layers know what
			 * happened.
			 */
			res = EC_MKBP_FP_ERR_MATCH_NO_INTERNAL;
			stats_.timestamps_invalid |= FPSTATS_MATCHING_INV;
		}

		fp_led::update_match(fp_match_success(res));

		if (res == EC_MKBP_FP_ERR_MATCH_YES_UPDATED) {
			ctx_.templ_dirty |= updated;
		}
	} else {
		CPRINTS("No enrolled templates");
		res = EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES;
		stats_.timestamps_invalid |= FPSTATS_MATCHING_INV;
	}

	stats_.matching_time_us = time_since32(t0);
	return make_fp_match_event(res, fgr);
}

bool FpSensorStateMachine::process_finger_capture(const CaptureRequest &capture)
{
	const timestamp_t t0 = get_time();
	ctx_.current_capture_type.reset();

	CPRINTS("Capturing ...");
	const int res =
		fp_acquire_image(image_buffer_.data(), capture.capture_type);
	stats_.capture_time_us = time_since32(t0);
	if (res != 0) {
		stats_.timestamps_invalid |= FPSTATS_CAPTURE_INV;
		return false;
	}

	ctx_.current_capture_type = capture.capture_type;

#ifndef CONFIG_ZEPHYR
	/* Clean up SPI before clocking up to avoid hang on the dsb
	 * in dma_go. Ignore the return value to let the WDT reboot
	 * the MCU (and avoid getting trapped in the loop).
	 * b/112781659 */
	const int spi_res = spi_transaction_flush(&spi_devices[0]);
	if (spi_res) {
		CPRINTS("Failed to flush SPI: 0x%x", spi_res);
	}
#endif

	/* we need CPU power to do the computations */
	ScopedFastCpu fast_cpu;

	const uint32_t evt = std::visit(
		Overloaded{
			[](const CaptureOnly &) -> uint32_t {
				return EC_MKBP_FP_IMAGE_READY;
			},
			[this](const EnrollImage &) -> uint32_t {
				return process_enroll();
			},
			[this](const MatchFinger &m) -> uint32_t {
				return process_match(m.allow_template_update);
			},
		},
		capture.kind);

	stats_.overall_time_us = time_since32(stats_.overall_t0);
	send_mkbp_event(evt);
	return true;
}

FpSensorStateMachine::State
FpSensorStateMachine::transition_on_config(const Command &cmd)
{
	return std::visit(
		Overloaded{
			[](const IdleCommand &c) -> State {
				enter_sensor_low_power();
				return IdleState{ .deepsleep = c.deepsleep };
			},
			[this](const ImmediateCaptureCommand &c) -> State {
				ctx_.current_capture_type.reset();
				if (fp_acquire_image(image_buffer_.data(),
						     c.capture_type) == 0) {
					ctx_.current_capture_type =
						c.capture_type;
				}
				send_mkbp_event(EC_MKBP_FP_IMAGE_READY);
				return IdleState{};
			},
			[](const DetectFingerCommand &c) -> State {
				fp_configure_detect();
				if (c.state.deepsleep) {
					enter_sensor_low_power();
				}
				if (c.state.waiting_for_irq()) {
					enable_sensor_irq();
				} else {
					enter_sensor_low_power();
				}
				return c.state;
			},
			[](const ResetSensorCommand &) -> State {
				fp_reset_and_clear_context();
				return IdleState{};
			},
			[](const MaintenanceCommand &) -> State {
				fp_maintenance();
				return IdleState{};
			},
			[this](const EncryptTemplateCommand &) -> State {
				ctx_.set_encrypted_template_ready(false);
				if (ctx_.template_encrypted_id.has_value()) {
					ScopedFastCpu fast_cpu;
					ctx_.set_encrypted_template_ready(
						encrypt_template(
							*ctx_.template_encrypted_id) ==
						EC_SUCCESS);
				}
				return IdleState{};
			},
			[this](const DecryptTemplateCommand &) -> State {
				{
					ScopedFastCpu fast_cpu;
					ctx_.set_decryption_result(
						fp_commit_template(
							ctx_.user_id));
				}
				return IdleState{};
			},
		},
		cmd);
}

void FpSensorStateMachine::handle_config_update()
{
	prepare_for_mode_change();

	const FpSensorMode requested_mode(ctx_.sensor_mode);
	const bool session_requested =
		requested_mode.has_any(FP_MODE_ENROLL_SESSION);
	if (session_requested != enroll_session_active_) {
		if (session_requested) {
			enroll_session_active_ = (fp_enrollment_begin() == 0);
		} else {
			abort_enroll_session();
		}
	}

	const Command cmd = parse_command(requested_mode);
	state_ = transition_on_config(cmd);
	const FpSensorMode active_mode = to_sensor_mode();
	ctx_.sensor_mode = active_mode.raw();

	if (!std::holds_alternative<ImmediateCaptureCommand>(cmd)) {
		fp_btn_ign_out::update(active_mode);
		fp_led::update_mode(active_mode);
	}
}

FpSensorStateMachine::State
FpSensorStateMachine::transition_on_sensor_or_timer(IdleState state,
						    uint32_t evt)
{
	enter_sensor_low_power(evt);
	return state;
}

FpSensorStateMachine::State
FpSensorStateMachine::transition_on_sensor_or_timer(DetectFingerState state,
						    uint32_t evt)
{
	const finger_state st = fp_finger_status();
	if (st == FINGER_PRESENT && state.notify_finger_down) {
		CPRINTS("Finger!");
		state.notify_finger_down = false;
		send_mkbp_event(EC_MKBP_FP_FINGER_DOWN);
	}
	if (st == FINGER_NONE && state.notify_finger_up) {
		state.notify_finger_up = false;
		send_mkbp_event(EC_MKBP_FP_FINGER_UP);
	}
	if (st == FINGER_PRESENT && state.capture.has_value()) {
		CPRINTS("Finger down (capture mode)!");
		/*
		 * Send FINGER_DOWN immediately upon detection
		 * during a capture session so the host can
		 * accurately measure interaction latency.
		 */
		send_mkbp_event(EC_MKBP_FP_FINGER_DOWN);
		if (process_finger_capture(*state.capture)) {
			state.capture.reset();
		}
	}

	if (state.waiting_for_irq()) {
		fp_configure_detect();
		enable_sensor_irq();
		return state;
	}

	enter_sensor_low_power(evt);
	if (state.notify_finger_up) {
		return state;
	}
	return IdleState{ .deepsleep = state.deepsleep };
}

void FpSensorStateMachine::handle_sensor_or_timer_event(uint32_t evt)
{
	stats_.overall_t0 = get_time();
	prepare_for_sensor_or_timer_event(evt);

	state_ = std::visit(
		[this, evt](auto s) -> State {
			return transition_on_sensor_or_timer(s, evt);
		},
		state_);
	const FpSensorMode active_mode = to_sensor_mode();
	ctx_.sensor_mode = active_mode.raw();

	fp_btn_ign_out::update(active_mode);
	fp_led::update_mode(active_mode);
}

#endif /* HAVE_FP_PRIVATE_DRIVER */

void FpSensorStateMachine::handle_event(uint32_t evt)
{
#ifdef HAVE_FP_PRIVATE_DRIVER
	if (evt & TASK_EVENT_UPDATE_CONFIG) {
		handle_config_update();
	} else if (evt & (TASK_EVENT_SENSOR_IRQ | TASK_EVENT_TIMER)) {
		handle_sensor_or_timer_event(evt);
	}
#else
	send_mkbp_event(evt);
#endif
}

extern "C" void fp_task(void)
{
	state_machine.run();
}

static enum ec_host_cmd_status
fp_command_info(struct ec_host_cmd_handler_args *args)
{
	struct ec_response_fp_info_v3 *r =
		static_cast<ec_response_fp_info_v3 *>(args->output_buf);
	size_t response_size =
		sizeof(struct ec_response_fp_info_v3) +
		FP_MAX_CAPTURE_TYPES * sizeof(struct fp_image_frame_params_v2);

	if (response_size > args->output_buf_max)
		return EC_HOST_CMD_OVERFLOW;

	/*
	 * The sensor may have fewer than FP_MAX_CAPTURE_TYPES number of
	 * captures, in which case the buffer will only be partially written. We
	 * want to make sure the rest of the buffer does not leak any data.
	 */
	memset(r, 0, response_size);

#ifdef HAVE_FP_PRIVATE_DRIVER
	if (fp_sensor_get_info(r, response_size) < 0)
#endif
		return EC_HOST_CMD_UNAVAILABLE;

	r->template_info.template_size = FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE;
	r->template_info.template_max = FP_MAX_FINGER_COUNT;
	r->template_info.template_valid = global_context.templ_valid;
	r->template_info.template_dirty = global_context.templ_dirty;
	r->template_info.template_version = FP_TEMPLATE_FORMAT_VERSION;

	args->output_buf_size = response_size;

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_RESP_ONLY(EC_CMD_FP_INFO, fp_command_info, EC_VER_MASK(3),
			      struct ec_response_fp_info_v3);

BUILD_ASSERT(FP_CONTEXT_NONCE_BYTES == 12);

namespace
{

template <typename... Spans> void fill_random_bytes(Spans &&...spans)
{
	trng_init();
	([](std::span<uint8_t> s) { trng_rand_bytes(s.data(), s.size()); }(
		 spans),
	 ...);
	trng_exit();
}

} /* namespace */

static enum ec_error_list encrypt_template(FpTemplateId fgr)
{
	enum ec_error_list ret;

	fp_enc_buffer = {};

	if (fgr.raw() >= global_context.templ_valid)
		return EC_ERROR_UNAVAILABLE;

	/*
	 * The beginning of the buffer contains nonce, encryption_salt
	 * and tag.
	 */
	struct ec_fp_template_encryption_metadata *enc_info =
		&fp_enc_buffer.metadata;

	CleanseWrapper<std::array<uint8_t, FP_CONTEXT_NONCE_BYTES> > nonce{};
	CleanseWrapper<std::array<uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES> >
		encryption_salt{};
	CleanseWrapper<std::array<uint8_t, FP_CONTEXT_TAG_BYTES> > tag{};

	fill_random_bytes(nonce, encryption_salt);

	if (fgr == global_context.template_newly_enrolled) {
		/*
		 * Newly enrolled templates need new positive match
		 * salt, new positive match secret and new validation
		 * value.
		 */
		global_context.template_newly_enrolled.reset();
		fill_random_bytes(
			global_context.fp_positive_match_salt[fgr.raw()]);
	}

	FpEncryptionKey key;
	ret = derive_encryption_key(key, encryption_salt,
				    global_context.user_id,
				    global_context.tpm_seed);
	if (ret != EC_SUCCESS) {
		CPRINTS("fgr%d: Failed to derive key", fgr.raw());
		return EC_ERROR_UNAVAILABLE;
	}

	/*
	 * Copy the payload to |fp_enc_buffer| where it will be
	 * encrypted in-place.
	 */
	fp_enc_buffer.fp_template = fp_template[fgr.raw()];
	fp_enc_buffer.positive_match_salt =
		global_context.fp_positive_match_salt[fgr.raw()];

	/* Encrypt the secret blob in-place. */
	std::span payload = fp_enc_buffer.payload();
	ret = aes_128_gcm_encrypt(key, payload, payload, nonce, tag);
	if (ret != EC_SUCCESS) {
		fp_enc_buffer.clear();
		CPRINTS("fgr%d: Failed to encrypt template", fgr.raw());
		return EC_ERROR_UNAVAILABLE;
	}

	enc_info->struct_version = FP_TEMPLATE_FORMAT_VERSION;
	std::ranges::copy(nonce, enc_info->nonce);
	std::ranges::copy(encryption_salt, enc_info->encryption_salt);
	std::ranges::copy(tag, enc_info->tag);

	global_context.clear_template_dirty(fgr);

	return EC_SUCCESS;
}

test_export_static enum ec_status get_frame(uint32_t offset, uint32_t size,
					    uint8_t *output)
{
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	const auto frame = global_context.captured_frame(fp_buffer);
	if (!frame.has_value())
		return EC_RES_INVALID_PARAM;

	const auto frame_chunk = checked_subspan(*frame, offset, size);
	if (!frame_chunk.has_value())
		return EC_RES_INVALID_PARAM;

	std::ranges::copy(*frame_chunk, output);

	return EC_RES_SUCCESS;
}

/* TODO(b/471160577): Remove FP_FRAME v0 after migration is completed. */
static enum ec_host_cmd_status
fp_command_frame_v0(struct ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_frame *>(args->input_buf);
	auto *out = static_cast<uint8_t *>(args->output_buf);
	uint16_t idx = FP_FRAME_GET_BUFFER_INDEX(params->offset);
	uint32_t offset = params->offset & FP_FRAME_OFFSET_MASK;
	uint32_t size = params->size;

	if (size > args->output_buf_max)
		return EC_HOST_CMD_INVALID_PARAM;

	if (idx == FP_FRAME_INDEX_RAW_IMAGE) {
		/* The host requested a frame. */
		enum ec_status ret = get_frame(offset, size, out);
		if (ret != EC_RES_SUCCESS) {
			return static_cast<enum ec_host_cmd_status>(ret);
		}

		args->output_buf_size = size;
		return EC_HOST_CMD_SUCCESS;
	}

	/* The host requested a template. */

	/* Encryption or decryption is in progress. */
	if (global_context.is_crypto_in_progress()) {
		return EC_HOST_CMD_BUSY;
	}

	/* Templates are numbered from 1 in this host request. */
	const auto fgr = FpTemplateId::create_enrolled(
		idx - FP_FRAME_INDEX_TEMPLATE, global_context.templ_valid);
	if (!fgr.has_value())
		return static_cast<enum ec_host_cmd_status>(fgr.error());

	auto enc_chunk =
		checked_subspan(fp_enc_buffer.as_bytes(), offset, size);
	if (!enc_chunk.has_value())
		return EC_HOST_CMD_INVALID_PARAM;

	if (!offset) {
		/* Host has requested the first chunk, do the encryption. */
		if (!global_context.try_acquire_encryption_slot())
			return EC_HOST_CMD_BUSY;

		ScopedFastCpu fast_cpu;

		if (encrypt_template(*fgr) != EC_SUCCESS) {
			return EC_HOST_CMD_UNAVAILABLE;
		}
	}
	std::ranges::copy(*enc_chunk, out);
	args->output_buf_size = size;

	return EC_HOST_CMD_SUCCESS;
}

/*
 * We are modifying variables in the global_context from this function running
 * in the HOSTCMD task and encrypt_template() running in the FPSENSOR task,
 * without protecting these data.
 *
 * This works because:
 * - We use single-core MCUs
 * - HOSTCMD task has higher priority than FPSENSOR task.
 * - There could be only one host command running at the time.
 * - Host commands cannot be interrupted by other tasks.
 * - We are checking only one bit in global_context.sensor_mode,
 *   so we are not prone to partial (non-atomic) writes/reads.
 *
 * TODO(b/479824917): Unfortunately, lack of any synchronization is a general
 * problem with FPSENSOR architecture. All fingerprint related work should be
 * done by FPSENSOR task, HOSTCMD role should be limited to validating input
 * (if possible), posting work to FPSENSOR task and waiting for result, if
 * needed.
 */
static enum ec_host_cmd_status
fp_command_frame_v1(struct ec_host_cmd_handler_args *args)
{
	const auto *params = static_cast<const struct ec_params_fp_frame_v1 *>(
		args->input_buf);
	auto *out = static_cast<uint8_t *>(args->output_buf);
	uint32_t offset = params->offset;
	uint32_t size = params->size;
	enum ec_status status;

	if (size > args->output_buf_max)
		return EC_HOST_CMD_INVALID_PARAM;

	switch (params->cmd) {
	case FP_FRAME_GET_RAW_IMAGE:
		/* The host requested a frame. */
		status = get_frame(offset, size, out);
		if (status != EC_RES_SUCCESS) {
			return static_cast<enum ec_host_cmd_status>(status);
		}

		args->output_buf_size = size;
		return EC_HOST_CMD_SUCCESS;
	case FP_FRAME_ENCRYPT_TEMPLATE: {
		/*
		 * Do not change the content of fp_enc_buffer if the encryption
		 * or decryption is in progress.
		 */
		if (global_context.is_crypto_in_progress()) {
			return EC_HOST_CMD_BUSY;
		}

		const auto fgr = FpTemplateId::create_enrolled(
			params->index, global_context.templ_valid);
		if (!fgr.has_value())
			return static_cast<enum ec_host_cmd_status>(
				fgr.error());

		if (!global_context.try_acquire_encryption_slot())
			return EC_HOST_CMD_BUSY;

		global_context.request_template_encryption(*fgr);
		if (!fp_set_sensor_mode(FP_MODE_ENCRYPT_TEMPLATE).has_value()) {
			return EC_HOST_CMD_ERROR;
		}
		break;
	}
	case FP_FRAME_GET_ENCRYPTED_TEMPLATE: {
		/* Encryption or decryption is still running */
		if (global_context.is_crypto_in_progress()) {
			return EC_HOST_CMD_BUSY;
		}

		/*
		 * Encrypted template not available (or encryption finished
		 * with error)
		 */
		if (!global_context.encrypted_template_ready()) {
			return EC_HOST_CMD_UNAVAILABLE;
		}

		/* Validate data request */
		auto enc_chunk =
			checked_subspan(fp_enc_buffer.as_bytes(), offset, size);
		if (!enc_chunk.has_value())
			return EC_HOST_CMD_INVALID_PARAM;

		/* Encryption succeeded */
		std::ranges::copy(*enc_chunk, out);
		args->output_buf_size = size;

		break;
	}
	}

	return EC_HOST_CMD_SUCCESS;
}

static enum ec_host_cmd_status
fp_command_frame(struct ec_host_cmd_handler_args *args)
{
	if (args->version == 1) {
		return fp_command_frame_v1(args);
	}

	return fp_command_frame_v0(args);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_FRAME, fp_command_frame,
			     EC_VER_MASK(0) | EC_VER_MASK(1),
			     SMALLEST_TYPE(struct ec_params_fp_frame,
					   struct ec_params_fp_frame_v1));

static enum ec_status fp_command_stats(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<struct ec_response_fp_stats *>(args->response);
	const FpSensorStats &stats = state_machine.stats();

	r->capture_time_us = stats.capture_time_us;
	r->matching_time_us = stats.matching_time_us;
	r->overall_time_us = stats.overall_time_us;
	r->overall_t0.lo = stats.overall_t0.le.lo;
	r->overall_t0.hi = stats.overall_t0.le.hi;
	r->timestamps_invalid = stats.timestamps_invalid;
	r->template_matched = static_cast<int8_t>(
		stats.template_matched.transform(&FpTemplateId::raw)
			.value_or(static_cast<uint16_t>(FP_NO_SUCH_TEMPLATE)));

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_STATS, fp_command_stats, EC_VER_MASK(0));

static enum ec_status fp_commit_template(std::span<const uint8_t> context)
{
	ScopedFastCpu fast_cpu;

	const auto id = FpTemplateId::create(global_context.templ_valid);
	if (!id.has_value())
		return EC_RES_OVERFLOW;

	/*
	 * The complete encrypted template has been received, start
	 * decryption.
	 */
	fp_clear_finger_context(*id);
	/*
	 * The beginning of the buffer contains nonce, encryption_salt
	 * and tag.
	 */
	struct ec_fp_template_encryption_metadata *enc_info =
		&fp_enc_buffer.metadata;
	if (!fp_enc_buffer.has_valid_metadata()) {
		CPRINTS("Invalid template format %d", enc_info->struct_version);
		CPRINTS("fgr%d: Template format not supported", id->raw());
		return EC_RES_INVALID_PARAM;
	}

	FpEncryptionKey key;
	enum ec_error_list ret =
		derive_encryption_key(key, enc_info->encryption_salt, context,
				      global_context.tpm_seed);
	if (ret != EC_SUCCESS) {
		CPRINTS("fgr%d: Failed to derive key", id->raw());
		return EC_RES_UNAVAILABLE;
	}

	/* Decrypt the secret blob in-place. */
	std::span payload = fp_enc_buffer.payload();
	ret = aes_128_gcm_decrypt(key, payload, payload, enc_info->nonce,
				  enc_info->tag);
	if (ret != EC_SUCCESS) {
		/* Don't leave partially decrypted data in the buffer! */
		fp_enc_buffer.clear();
		CPRINTS("fgr%d: Failed to decipher template", id->raw());
		return EC_RES_UNAVAILABLE;
	}

	if (!fp_enc_buffer.has_nontrivial_salt()) {
		/* Don't leave decrypted data in the buffer! */
		fp_enc_buffer.clear();
		CPRINTS("fgr%d: Trivial positive match salt.", id->raw());
		return EC_RES_INVALID_PARAM;
	}

	fp_template[id->raw()] = fp_enc_buffer.fp_template;
	global_context.fp_positive_match_salt[id->raw()] =
		fp_enc_buffer.positive_match_salt;

	/* Don't leave decrypted data in the buffer! */
	fp_enc_buffer.clear();

	global_context.templ_valid++;
	return EC_RES_SUCCESS;
}

static enum ec_status fp_command_template_v0(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_template *>(args->params);
	uint32_t size = params->size & ~FP_TEMPLATE_COMMIT;
	bool xfer_complete = params->size & FP_TEMPLATE_COMMIT;
	uint32_t offset = params->offset;

	/* Can we store one more template ? */
	if (!FpTemplateId::create(global_context.templ_valid).has_value())
		return EC_RES_OVERFLOW;

	/* Encryption or decryption is in progress. */
	if (global_context.is_crypto_in_progress()) {
		return EC_RES_BUSY;
	}

	if (args->params_size !=
	    size + offsetof(struct ec_params_fp_template, data))
		return EC_RES_INVALID_PARAM;
	if (!fp_enc_buffer.write_chunk(offset, std::span(params->data, size)))
		return EC_RES_INVALID_PARAM;

	if (xfer_complete) {
		return fp_commit_template(global_context.user_id);
	}

	return EC_RES_SUCCESS;
}

static enum ec_status fp_command_template_v1(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_template_v1 *>(
			args->params);

	switch (params->cmd) {
	case FP_TEMPLATE_LOAD: {
		uint32_t size = params->size;
		uint32_t offset = params->offset;

		/* Encryption or decryption is in progress. */
		if (global_context.is_crypto_in_progress()) {
			return EC_RES_BUSY;
		}

		/* Can we store one more template ? */
		if (!FpTemplateId::create(global_context.templ_valid)
			     .has_value()) {
			return EC_RES_OVERFLOW;
		}

		if (args->params_size !=
		    size + offsetof(struct ec_params_fp_template_v1, data)) {
			return EC_RES_INVALID_PARAM;
		}
		if (!fp_enc_buffer.write_chunk(offset,
					       std::span(params->data, size))) {
			return EC_RES_INVALID_PARAM;
		}

		/*
		 * We are going to modify 'fp_enc_buffer', so clear
		 * FP_ENCRYPTED_TEMPLATE_READY bit and 'template_encrypted_id'.
		 */
		global_context.clear_encrypted_template();

		return EC_RES_SUCCESS;
	}
	case FP_TEMPLATE_DECRYPT: {
		/* Encryption or decryption is in progress. */
		if (global_context.is_crypto_in_progress()) {
			return EC_RES_BUSY;
		}

		/* Start template decryption. */
		if (!fp_set_sensor_mode(FP_MODE_DECRYPT_TEMPLATE).has_value()) {
			global_context.set_decryption_result(EC_RES_ERROR);
			return EC_RES_ERROR;
		}

		global_context.set_decryption_result(EC_RES_BUSY);

		return EC_RES_SUCCESS;
	}
	case FP_TEMPLATE_GET_RESULT:
		/* Decryption is still running */
		if (global_context.has_any_mode(FP_MODE_DECRYPT_TEMPLATE)) {
			return EC_RES_BUSY;
		}

		return global_context.get_decryption_result();
	}

	return EC_RES_INVALID_PARAM;
}

static enum ec_status fp_command_template(struct host_cmd_handler_args *args)
{
	if (args->version == 1) {
		return fp_command_template_v1(args);
	}

	return fp_command_template_v0(args);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_TEMPLATE, fp_command_template,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static enum ec_status
fp_command_confirm_template(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_confirm_template *>(
			args->params);
	std::span<const uint8_t, FP_MAC_LENGTH> mac{ params->mac };

	/*
	 * After successful enrollment, the 'template_newly_enrolled' (new
	 * template id) must be equal to 'templ_valid'.
	 */
	if (global_context.template_newly_enrolled !=
	    FpTemplateId::create(global_context.templ_valid)) {
		return EC_RES_ERROR;
	}

	if (validate_request(global_context.auth_user_id_context(),
			     as_operation_span("enroll_finish"),
			     mac) != EC_SUCCESS) {
		return EC_RES_ACCESS_DENIED;
	}

	/* Add newly enrolled template to valid templates. */
	global_context.templ_valid++;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_CONFIRM_TEMPLATE, fp_command_confirm_template,
		     EC_VER_MASK(0));
