/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "assert.h"
#include "atomic.h"
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
#include "fpsensor/fpsensor_platform.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_state_machine.h"
#include "fpsensor/fpsensor_utils.h"
#include "host_command.h"
#include "link_defs.h"
#include "mkbp_event.h"
#include "scoped_fast_cpu.h"
#include "system.h"
#include "task.h"
#include "trng.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <variant>

#if !defined(CONFIG_RNG)
#error "fpsensor requires RNG"
#endif

#if defined(CONFIG_CROS_EC_RO)
#error "fpsensor code should not be in RO image."
#endif

static_assert(sizeof(ec_fp_template_encryption_metadata) % 4 == 0);

namespace
{

FpSensorStateMachine state_machine;

} /* namespace */

void FpSensorStateMachine::send_mkbp_event(uint32_t event)
{
	ctx_.add_events(event);
	mkbp_send_event(EC_MKBP_EVENT_FINGERPRINT);
}

FpSensorStateMachine::Command
FpSensorStateMachine::parse_command(FpSensorMode mode) noexcept
{
	if (!mode.is_finger_needed()) {
		return ImmediateCaptureCommand{ mode.capture_type() };
	}

	if (mode.has_any(FP_MODE_ANY_DETECT_FINGER)) {
		return DetectFingerCommand{
			.state =
				DetectFingerState{
					.notify_finger_down = mode.has_any(
						FP_MODE_FINGER_DOWN),
					.notify_finger_up =
						mode.has_any(FP_MODE_FINGER_UP),
					.deepsleep =
						mode.has_any(FP_MODE_DEEPSLEEP),
					.capture =
						CaptureRequest::from_mode(mode),
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
	fp_platform::lock_pm();
	CPRINTS("FP_SENSOR_SEL: %s",
		fp_sensor_type_to_str(fpsensor_detect_get_type()));

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
		uint32_t evt;
		{
#ifdef HAVE_FP_PRIVATE_DRIVER
			fp_platform::ScopedPmUnlock pm_unlock(
				!enroll_session_active_);
#endif
			/* Wait for a sensor IRQ or a new mode configuration */
			evt = task_wait_event(static_cast<int>(
				timeout()
					.transform(
						&std::chrono::microseconds::count)
					.value_or(-1)));
		}

		handle_event(evt);
	}
}

#ifdef HAVE_FP_PRIVATE_DRIVER

namespace
{

FpSensorStats::Duration elapsed_since(FpTimestamp start)
{
	return std::chrono::duration_cast<FpSensorStats::Duration>(
		FpClock::now() - start);
}

} /* namespace */

void FpTemplateStore::finish_enrollment(FpTemplateId id, bool auth_enabled)
{
	template_newly_enrolled = id;
	positive_match_secret_state.enable(id);

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
	if (!auth_enabled) {
		templ_valid++;
	}
}

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
	const auto enroll_id = ctx_.templates.next_free_id();
	if (!enroll_id.has_value()) {
		CPRINTS("Error: Max templates reached.");
		abort_enroll_session();
		return make_fp_enroll_event(EC_MKBP_FP_ERR_ENROLL_INTERNAL);
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
		return make_fp_enroll_event(EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	}
	ctx_.templates.mark_dirty(*enroll_id);
	if (percent == 100) {
		res = finish_enroll_session(
			ctx_.templates.templates[enroll_id->raw()]);
		if (res != 0) {
			res = EC_MKBP_FP_ERR_ENROLL_INTERNAL;
		} else {
			ctx_.templates.finish_enrollment(
				*enroll_id, ctx_.crypto.auth_enabled());
		}
	}
	return make_fp_enroll_event(res, percent);
}

uint32_t FpSensorStateMachine::process_match(bool allow_template_update)
{
	const FpTimestamp t0 = FpClock::now();
	int res = -1;
	uint32_t updated = 0;
	int32_t fgr = FP_NO_SUCH_TEMPLATE;
	stats_.capture_invalid = false;
	stats_.matching_invalid = false;
	stats_.template_matched.reset();

	/* match finger against current templates */
	ctx_.templates.positive_match_secret_state.disable();

	CPRINTS("Matching/%d ...", ctx_.templates.templ_valid);
	if (ctx_.templates.templ_valid == 0) {
		CPRINTS("No enrolled templates");
		stats_.matching_invalid = true;
		stats_.matching_time = elapsed_since(t0);
		return make_fp_match_event(EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES,
					   fgr);
	}

	res = fp_finger_match(ctx_.templates.templates[0].data(),
			      ctx_.templates.templ_valid,
			      ctx_.frame.image_buffer.data(),
			      allow_template_update, &fgr, &updated);
	CPRINTS("Match =>%d (finger %d)", res, fgr);

	if (fp_match_success(res)) {
		/*
		 * Match succeded! Let's check if template number is valid. If
		 * it is not valid, overwrite result with
		 * EC_MKBP_FP_ERR_MATCH_NO_INTERNAL.
		 */
		if (const auto match_id = FpTemplateId::create(fgr);
		    match_id.has_value()) {
			stats_.template_matched = *match_id;
			ctx_.templates.positive_match_secret_state.enable(
				*match_id);
		} else {
			res = EC_MKBP_FP_ERR_MATCH_NO_INTERNAL;
			stats_.matching_invalid = true;
		}
	} else if (res < 0) {
		/*
		 * Negative result means that there is a problem with code
		 * responsible for matching. Overwrite it with
		 * MATCH_NO_INTERNAL to let upper layers know what happened.
		 */
		res = EC_MKBP_FP_ERR_MATCH_NO_INTERNAL;
		stats_.matching_invalid = true;
	}

	fp_led::update_match(fp_match_success(res));

	if (res == EC_MKBP_FP_ERR_MATCH_YES_UPDATED) {
		ctx_.templates.mark_dirty_mask(updated);
	}

	stats_.matching_time = elapsed_since(t0);
	return make_fp_match_event(res, fgr);
}

bool FpSensorStateMachine::process_finger_capture(const CaptureRequest &capture)
{
	const FpTimestamp t0 = FpClock::now();
	ctx_.frame.begin_capture();

	CPRINTS("Capturing ...");
	const int res = fp_acquire_image(ctx_.frame.image_buffer.data(),
					 capture.capture_type.raw());
	stats_.capture_time = elapsed_since(t0);
	if (res != 0) {
		stats_.capture_invalid = true;
		return false;
	}

	ctx_.frame.finish_capture(capture.capture_type);

	fp_platform::flush_spi_after_acquire();

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

	stats_.overall_time = elapsed_since(stats_.overall_t0);
	send_mkbp_event(evt);
	return true;
}

FpSensorStateMachine::State
FpSensorStateMachine::transition_on_config(const Command &cmd)
{
	return std::visit(
		Overloaded{
			[](const IdleCommand &c) -> State {
				fp_platform::enter_sensor_low_power();
				return IdleState{ .deepsleep = c.deepsleep };
			},
			[this](const ImmediateCaptureCommand &c) -> State {
				ctx_.frame.begin_capture();
				if (fp_acquire_image(
					    ctx_.frame.image_buffer.data(),
					    c.capture_type.raw()) == 0) {
					ctx_.frame.finish_capture(
						c.capture_type);
				}
				send_mkbp_event(EC_MKBP_FP_IMAGE_READY);
				return IdleState{};
			},
			[](const DetectFingerCommand &c) -> State {
				fp_configure_detect();
				if (c.state.deepsleep) {
					fp_platform::enter_sensor_low_power();
				}
				if (c.state.waiting_for_irq()) {
					fp_platform::enable_sensor_irq();
				} else {
					fp_platform::enter_sensor_low_power();
				}
				return c.state;
			},
			[this](const ResetSensorCommand &) -> State {
				ctx_.reset_and_clear_context();
				return IdleState{};
			},
			[](const MaintenanceCommand &) -> State {
				fp_maintenance();
				return IdleState{};
			},
			[this](const EncryptTemplateCommand &) -> State {
				ctx_.encrypt_requested_template();
				return IdleState{};
			},
			[this](const DecryptTemplateCommand &) -> State {
				ctx_.decrypt_and_commit_template();
				return IdleState{};
			},
		},
		cmd);
}

void FpSensorStateMachine::handle_config_update()
{
	fp_platform::prepare_for_mode_change();

	const FpSensorMode requested_mode = ctx_.mode();
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
	fp_platform::enter_sensor_low_power(evt);
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
		fp_platform::enable_sensor_irq();
		return state;
	}

	fp_platform::enter_sensor_low_power(evt);
	if (state.notify_finger_up) {
		return state;
	}
	return IdleState{ .deepsleep = state.deepsleep };
}

void FpSensorStateMachine::handle_sensor_or_timer_event(uint32_t evt)
{
	stats_.overall_t0 = FpClock::now();
	fp_platform::prepare_for_sensor_or_timer_event(evt);

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

extern "C" void fp_task()
{
	state_machine.run();
}

namespace
{

ec_host_cmd_status fp_command_info(ec_host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_info_v3 *>(args->output_buf);
	constexpr size_t kResponseSize = sizeof(FpSensorInfoBuffer);

	if (kResponseSize > args->output_buf_max) {
		return EC_HOST_CMD_OVERFLOW;
	}

	/*
	 * The sensor may have fewer than FP_MAX_CAPTURE_TYPES number of
	 * captures, in which case the buffer will only be partially written. We
	 * want to make sure the rest of the buffer does not leak any data.
	 */
	std::ranges::fill(std::span(static_cast<uint8_t *>(args->output_buf),
				    kResponseSize),
			  0);

#ifdef HAVE_FP_PRIVATE_DRIVER
	if (fp_sensor_get_info(r, kResponseSize) < 0) {
		return EC_HOST_CMD_UNAVAILABLE;
	}

	r->template_info = global_context.templates.to_template_info();
	args->output_buf_size = kResponseSize;
	return EC_HOST_CMD_SUCCESS;
#else
	return EC_HOST_CMD_UNAVAILABLE;
#endif
}
EC_HOST_CMD_HANDLER_RESP_ONLY(EC_CMD_FP_INFO, fp_command_info, EC_VER_MASK(3),
			      ec_response_fp_info_v3);

static_assert(FP_CONTEXT_NONCE_BYTES == 12);

template <typename... Spans> void fill_random_bytes(Spans &&...spans)
{
	trng_init();
	([](std::span<uint8_t> s) { trng_rand_bytes(s.data(), s.size()); }(
		 spans),
	 ...);
	trng_exit();
}

} /* namespace */

std::expected<void, ec_error_list> FpTemplateStore::encrypt(
	FpTemplateId fgr,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	*enc_buf = {};

	if (fgr.raw() >= templ_valid) {
		return std::unexpected(EC_ERROR_UNAVAILABLE);
	}

	CleanseWrapper<std::array<uint8_t, FP_CONTEXT_NONCE_BYTES> > nonce{};
	CleanseWrapper<std::array<uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES> >
		encryption_salt{};
	CleanseWrapper<std::array<uint8_t, FP_CONTEXT_TAG_BYTES> > tag{};

	fill_random_bytes(nonce, encryption_salt);

	if (fgr == template_newly_enrolled) {
		/*
		 * Newly enrolled templates need new positive match
		 * salt, new positive match secret and new validation
		 * value.
		 */
		template_newly_enrolled.reset();
		fill_random_bytes(fp_positive_match_salt[fgr.raw()]);
	}

	const auto key =
		derive_encryption_key(encryption_salt, user_id, tpm_seed);
	if (!key.has_value()) {
		CPRINTS("fgr%d: Failed to derive key", fgr.raw());
		return std::unexpected(EC_ERROR_UNAVAILABLE);
	}

	/*
	 * Copy the payload to |enc_buf| where it will be encrypted in-place.
	 */
	enc_buf->fp_template = templates[fgr.raw()];
	enc_buf->positive_match_salt = fp_positive_match_salt[fgr.raw()];

	std::span payload = enc_buf->payload();
	if (!aes_128_gcm_encrypt(*key, payload, payload, nonce, tag)
		     .has_value()) {
		enc_buf->clear();
		CPRINTS("fgr%d: Failed to encrypt template", fgr.raw());
		return std::unexpected(EC_ERROR_UNAVAILABLE);
	}

	auto &enc_info = enc_buf->metadata;
	enc_info.struct_version = FP_TEMPLATE_FORMAT_VERSION;
	std::ranges::copy(nonce, enc_info.nonce);
	std::ranges::copy(encryption_salt, enc_info.encryption_salt);
	std::ranges::copy(tag, enc_info.tag);
	clear_dirty(fgr);
	return {};
}

void fpsensor_context::encrypt_requested_template()
{
	crypto.encrypted_template_ready = false;
	if (templates.template_encrypted_id.has_value()) {
		ScopedFastCpu fast_cpu;
		crypto.encrypted_template_ready =
			templates
				.encrypt(*templates.template_encrypted_id,
					 crypto.user_id, crypto.tpm_seed)
				.has_value();
	}
}

test_export_static std::expected<void, ec_status>
get_frame(uint32_t offset, std::span<uint8_t> output)
{
	if (system_is_locked()) {
		return std::unexpected(EC_RES_ACCESS_DENIED);
	}

	const auto frame = global_context.frame.captured_frame();
	if (!frame.has_value()) {
		return std::unexpected(frame.error());
	}

	return checked_subspan(*frame, offset, output.size())
		.transform([output](std::span<const uint8_t> chunk) {
			std::ranges::copy(chunk, output.begin());
		})
		.transform_error(
			[](ec_error_list) { return EC_RES_INVALID_PARAM; });
}

namespace
{

ec_host_cmd_status copy_frame_to_host_output(uint32_t offset,
					     std::span<uint8_t> output,
					     uint16_t &output_buf_size)
{
	return get_frame(offset, output)
		.transform([&] { output_buf_size = output.size(); })
		.transform_error([](ec_status err) {
			return static_cast<ec_host_cmd_status>(err);
		})
		.error_or(EC_HOST_CMD_SUCCESS);
}

} /* namespace */

std::expected<void, ec_host_cmd_status>
fpsensor_context::get_encrypted_template_v0(uint16_t idx, uint32_t offset,
					    std::span<uint8_t> output)
{
	/* Encryption or decryption is in progress. */
	if (is_crypto_in_progress()) {
		return std::unexpected(EC_HOST_CMD_BUSY);
	}

	/* Templates are numbered from 1 in this host request. */
	const auto fgr =
		FpTemplateId::create_enrolled(idx, templates.templ_valid);
	if (!fgr.has_value()) {
		return std::unexpected(
			static_cast<ec_host_cmd_status>(fgr.error()));
	}

	if (!templates.enc_buf->validate_offset(offset, output.size())
		     .has_value()) {
		return std::unexpected(EC_HOST_CMD_INVALID_PARAM);
	}

	if (offset == 0) {
		/* Host has requested the first chunk, do the encryption. */
		if (const auto slot = templates.try_acquire_encryption_slot();
		    !slot.has_value()) {
			return std::unexpected(slot.error());
		}

		ScopedFastCpu fast_cpu;

		if (!templates.encrypt(*fgr, crypto.user_id, crypto.tpm_seed)
			     .has_value()) {
			return std::unexpected(EC_HOST_CMD_UNAVAILABLE);
		}
	}

	return templates.enc_buf->read_chunk(offset, output)
		.transform_error([](ec_error_list) {
			return EC_HOST_CMD_INVALID_PARAM;
		});
}

std::expected<void, ec_host_cmd_status>
fpsensor_context::start_template_encryption(uint16_t idx)
{
	/*
	 * Do not change the content of fp_enc_buffer if the encryption
	 * or decryption is in progress.
	 */
	if (is_crypto_in_progress()) {
		return std::unexpected(EC_HOST_CMD_BUSY);
	}

	const auto fgr =
		FpTemplateId::create_enrolled(idx, templates.templ_valid);
	if (!fgr.has_value()) {
		return std::unexpected(
			static_cast<ec_host_cmd_status>(fgr.error()));
	}

	if (const auto slot = templates.try_acquire_encryption_slot();
	    !slot.has_value()) {
		return std::unexpected(slot.error());
	}

	request_template_encryption(*fgr);
	if (!fp_set_sensor_mode(FP_MODE_ENCRYPT_TEMPLATE, std::nullopt, *this)
		     .has_value()) {
		return std::unexpected(EC_HOST_CMD_ERROR);
	}

	return {};
}

std::expected<void, ec_host_cmd_status>
fpsensor_context::get_encrypted_template_chunk(uint32_t offset,
					       std::span<uint8_t> output) const
{
	/* Encryption or decryption is still running */
	if (is_crypto_in_progress()) {
		return std::unexpected(EC_HOST_CMD_BUSY);
	}

	/*
	 * Encrypted template not available (or encryption finished
	 * with error)
	 */
	if (!crypto.encrypted_template_ready) {
		return std::unexpected(EC_HOST_CMD_UNAVAILABLE);
	}

	return templates.enc_buf->read_chunk(offset, output)
		.transform_error([](ec_error_list) {
			return EC_HOST_CMD_INVALID_PARAM;
		});
}

namespace
{

/* TODO(b/471160577): Remove FP_FRAME v0 after migration is completed. */
ec_host_cmd_status fp_command_frame_v0(ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_frame *>(args->input_buf);
	const auto loc = FpFrameV0Offset::unpack(params->offset);
	const uint32_t size = params->size;

	if (size > args->output_buf_max) {
		return EC_HOST_CMD_INVALID_PARAM;
	}

	const std::span<uint8_t> output(
		static_cast<uint8_t *>(args->output_buf), size);

	if (loc.is_raw_image()) {
		/* The host requested a frame. */
		return copy_frame_to_host_output(loc.offset, output,
						 args->output_buf_size);
	}

	/* The host requested a template. */
	return global_context
		.get_encrypted_template_v0(loc.template_index(), loc.offset,
					   output)
		.transform([&] { args->output_buf_size = output.size(); })
		.error_or(EC_HOST_CMD_SUCCESS);
}

/*
 * We are modifying variables in the global_context from this function running
 * in the HOSTCMD task and encrypt_requested_template() running in the FPSENSOR
 * task, without protecting these data.
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
ec_host_cmd_status fp_command_frame_v1(ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_frame_v1 *>(args->input_buf);
	const uint32_t offset = params->offset;
	const uint32_t size = params->size;

	if (size > args->output_buf_max) {
		return EC_HOST_CMD_INVALID_PARAM;
	}

	const std::span<uint8_t> output(
		static_cast<uint8_t *>(args->output_buf), size);

	switch (params->cmd) {
	case FP_FRAME_GET_RAW_IMAGE:
		/* The host requested a frame. */
		return copy_frame_to_host_output(offset, output,
						 args->output_buf_size);
	case FP_FRAME_ENCRYPT_TEMPLATE:
		return global_context.start_template_encryption(params->index)
			.error_or(EC_HOST_CMD_SUCCESS);
	case FP_FRAME_GET_ENCRYPTED_TEMPLATE:
		return global_context
			.get_encrypted_template_chunk(offset, output)
			.transform(
				[&] { args->output_buf_size = output.size(); })
			.error_or(EC_HOST_CMD_SUCCESS);
	}

	return EC_HOST_CMD_SUCCESS;
}

ec_host_cmd_status fp_command_frame(ec_host_cmd_handler_args *args)
{
	if (args->version == 1) {
		return fp_command_frame_v1(args);
	}

	return fp_command_frame_v0(args);
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_FRAME, fp_command_frame,
			     EC_VER_MASK(0) | EC_VER_MASK(1),
			     SMALLEST_TYPE(ec_params_fp_frame,
					   ec_params_fp_frame_v1));

ec_status fp_command_stats(host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_stats *>(args->response);

	*r = state_machine.stats().to_response();
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_STATS, fp_command_stats, EC_VER_MASK(0));

} /* namespace */

std::expected<void, ec_status>
FpTemplateStore::commit(std::span<const uint8_t> context,
			std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	ScopedFastCpu fast_cpu;

	const auto id = next_free_id();
	if (!id.has_value()) {
		return std::unexpected(id.error());
	}

	/*
	 * The complete encrypted template has been received, start
	 * decryption.
	 */
	clear_finger(*id);
	/*
	 * The beginning of the buffer contains nonce, encryption_salt
	 * and tag.
	 */
	const auto &enc_info = enc_buf->metadata;
	if (!enc_buf->has_valid_metadata()) {
		CPRINTS("Invalid template format %d", enc_info.struct_version);
		CPRINTS("fgr%d: Template format not supported", id->raw());
		return std::unexpected(EC_RES_INVALID_PARAM);
	}

	const auto key = derive_encryption_key(enc_info.encryption_salt,
					       context, tpm_seed);
	if (!key.has_value()) {
		CPRINTS("fgr%d: Failed to derive key", id->raw());
		enc_buf->clear();
		return std::unexpected(EC_RES_UNAVAILABLE);
	}

	/*
	 * Decrypt the secret blob in-place and ensure we never leave decrypted
	 * or partially decrypted data in |enc_buf| on any return path.
	 */
	std::span payload = enc_buf->payload();
	if (!aes_128_gcm_decrypt(*key, payload, payload, enc_info.nonce,
				 enc_info.tag)
		     .has_value()) {
		CPRINTS("fgr%d: Failed to decipher template", id->raw());
		enc_buf->clear();
		return std::unexpected(EC_RES_UNAVAILABLE);
	}

	if (!enc_buf->has_nontrivial_salt()) {
		CPRINTS("fgr%d: Trivial positive match salt.", id->raw());
		enc_buf->clear();
		return std::unexpected(EC_RES_INVALID_PARAM);
	}

	templates[id->raw()] = enc_buf->fp_template;
	fp_positive_match_salt[id->raw()] = enc_buf->positive_match_salt;
	templ_valid++;
	enc_buf->clear();
	return {};
}

std::expected<void, ec_status>
fpsensor_context::load_template_chunk(uint32_t offset,
				      std::span<const uint8_t> chunk)
{
	/* Encryption or decryption is in progress. */
	if (is_crypto_in_progress()) {
		return std::unexpected(EC_RES_BUSY);
	}

	/* Can we store one more template ? */
	if (const auto id = templates.next_free_id(); !id.has_value()) {
		return std::unexpected(id.error());
	}

	if (!templates.enc_buf->write_chunk(offset, chunk).has_value()) {
		return std::unexpected(EC_RES_INVALID_PARAM);
	}

	/*
	 * We are going to modify 'fp_enc_buffer', so clear
	 * FP_ENCRYPTED_TEMPLATE_READY bit and 'template_encrypted_id'.
	 */
	clear_encrypted_template();

	return {};
}

std::expected<void, ec_status> fpsensor_context::commit_template()
{
	return templates.commit(crypto.user_id, crypto.tpm_seed);
}

std::expected<void, ec_status> fpsensor_context::start_template_decryption()
{
	/* Encryption or decryption is in progress. */
	if (is_crypto_in_progress()) {
		return std::unexpected(EC_RES_BUSY);
	}

	/* Start template decryption. */
	if (!fp_set_sensor_mode(FP_MODE_DECRYPT_TEMPLATE, std::nullopt, *this)
		     .has_value()) {
		templates.set_decryption_result(EC_RES_ERROR);
		return std::unexpected(EC_RES_ERROR);
	}

	templates.set_decryption_result(EC_RES_BUSY);
	return {};
}

ec_status fpsensor_context::get_template_decryption_result() const noexcept
{
	/* Decryption is still running */
	if (has_any_mode(FP_MODE_DECRYPT_TEMPLATE)) {
		return EC_RES_BUSY;
	}

	return templates.get_decryption_result();
}

void fpsensor_context::decrypt_and_commit_template()
{
	templates.set_decryption_result(
		commit_template().error_or(EC_RES_SUCCESS));
}

namespace
{

ec_status fp_command_template_v0(host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_template *>(args->params);
	const auto [size, xfer_complete] =
		FpTemplateV0Size::unpack(params->size);
	const uint32_t offset = params->offset;

	if (args->params_size != size + offsetof(ec_params_fp_template, data)) {
		return EC_RES_INVALID_PARAM;
	}

	return global_context
		.load_template_chunk(offset, std::span(params->data, size))
		.and_then([xfer_complete]() -> std::expected<void, ec_status> {
			if (xfer_complete) {
				return global_context.commit_template();
			}
			return {};
		})
		.error_or(EC_RES_SUCCESS);
}

ec_status fp_command_template_v1(host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_template_v1 *>(args->params);

	switch (params->cmd) {
	case FP_TEMPLATE_LOAD: {
		const uint32_t size = params->size;
		const uint32_t offset = params->offset;

		if (args->params_size !=
		    size + offsetof(ec_params_fp_template_v1, data)) {
			return EC_RES_INVALID_PARAM;
		}

		return global_context
			.load_template_chunk(offset,
					     std::span(params->data, size))
			.error_or(EC_RES_SUCCESS);
	}
	case FP_TEMPLATE_DECRYPT:
		return global_context.start_template_decryption().error_or(
			EC_RES_SUCCESS);
	case FP_TEMPLATE_GET_RESULT:
		return global_context.get_template_decryption_result();
	}

	return EC_RES_INVALID_PARAM;
}

ec_status fp_command_template(host_cmd_handler_args *args)
{
	if (args->version == 1) {
		return fp_command_template_v1(args);
	}

	return fp_command_template_v0(args);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_TEMPLATE, fp_command_template,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

} /* namespace */

std::expected<void, ec_status>
fpsensor_context::confirm_template(std::span<const uint8_t, FP_MAC_LENGTH> mac)
{
	/*
	 * After successful enrollment, the 'template_newly_enrolled' (new
	 * template id) must be equal to 'templ_valid'.
	 */
	if (templates.template_newly_enrolled != templates.next_free_id()) {
		return std::unexpected(EC_RES_ERROR);
	}

	return validate_request(crypto.auth_user_id_context(), "enroll_finish",
				mac)
		.transform([this] {
			/* Add newly enrolled template to valid templates. */
			templates.templ_valid++;
		})
		.transform_error(
			[](ec_error_list) { return EC_RES_ACCESS_DENIED; });
}

namespace
{

ec_status fp_command_confirm_template(host_cmd_handler_args *args)
{
	const auto *params = static_cast<const ec_params_fp_confirm_template *>(
		args->params);

	return global_context.confirm_template(params->mac)
		.error_or(EC_RES_SUCCESS);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_CONFIRM_TEMPLATE, fp_command_confirm_template,
		     EC_VER_MASK(0));

} /* namespace */
