/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_state_driver.h"
#include "host_command.h"
#include "scoped_fast_cpu.h"
#include "task.h"
#include "util.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

#pragma GCC poison global_context

#if defined(CONFIG_CROS_EC_RO)
#error "fpsensor code should not be in RO image."
#endif

/* LCOV_EXCL_START */
__test_only void fp_task_simulate()
{
	constexpr int kTimeoutUs = -1;

	while (true) {
		task_wait_event(kTimeoutUs);
	}
}
/* LCOV_EXCL_STOP */

void FpSensorContext::reset_context() noexcept
{
	templates.reset();
	crypto.reset_user_context();
}

/**
 * @warning |frame.image_buffer| contains data used by the matching algorithm
 * that must be released by calling fp_sensor_deinit() first. Call
 * reset_and_clear_context() instead of calling this directly.
 */
void FpSensorContext::clear_context() noexcept
{
	reset_context();
	frame.clear();
}

void FpSensorContext::reset_and_clear_context()
{
	if (fp_sensor_deinit() != EC_SUCCESS) {
		CPRINTS("Failed to deinit sensor");
	}
	clear_context();
	if (fp_sensor_init() != EC_SUCCESS) {
		CPRINTS("Failed to init sensor");
	}
}

void FpSensorContext::encrypt_requested_template()
{
	crypto.encrypted_template_ready = false;
	if (templates.template_encrypted_id.has_value()) {
		ScopedFastCpu fast_cpu;
		crypto.encrypted_template_ready =
			templates
				.encrypt(*templates.template_encrypted_id,
					 crypto.user_id.bytes(),
					 crypto.tpm_seed.bytes())
				.has_value();
	}
}

std::expected<void, ec_host_cmd_status>
FpSensorContext::get_encrypted_template_v0(uint16_t idx, uint32_t offset,
					   std::span<uint8_t> output)
{
	/* Encryption or decryption is in progress. */
	if (sensor_mode.is_crypto_in_progress()) {
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
		if (const auto slot =
			    templates.encryption_rate_limiter.try_acquire();
		    !slot.has_value()) {
			return std::unexpected(slot.error());
		}

		ScopedFastCpu fast_cpu;

		if (!templates
			     .encrypt(*fgr, crypto.user_id.bytes(),
				      crypto.tpm_seed.bytes())
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
FpSensorContext::get_frame_v0(FpFrameV0Offset loc, std::span<uint8_t> output)
{
	if (loc.is_raw_image()) {
		return frame.get_raw_frame(loc.offset, output)
			.transform_error([](ec_status err) {
				return static_cast<ec_host_cmd_status>(err);
			});
	}

	return get_encrypted_template_v0(loc.template_index(), loc.offset,
					 output);
}

std::expected<void, ec_host_cmd_status>
FpSensorContext::start_template_encryption(uint16_t idx)
{
	/*
	 * Do not change the content of fp_enc_buffer if the encryption
	 * or decryption is in progress.
	 */
	if (sensor_mode.is_crypto_in_progress()) {
		return std::unexpected(EC_HOST_CMD_BUSY);
	}

	const auto fgr =
		FpTemplateId::create_enrolled(idx, templates.templ_valid);
	if (!fgr.has_value()) {
		return std::unexpected(
			static_cast<ec_host_cmd_status>(fgr.error()));
	}

	if (const auto slot = templates.encryption_rate_limiter.try_acquire();
	    !slot.has_value()) {
		return std::unexpected(slot.error());
	}

	request_template_encryption(*fgr);
	if (!set_sensor_mode(FP_MODE_ENCRYPT_TEMPLATE).has_value()) {
		return std::unexpected(EC_HOST_CMD_ERROR);
	}

	return {};
}

std::expected<void, ec_host_cmd_status>
FpSensorContext::get_encrypted_template_chunk(uint32_t offset,
					      std::span<uint8_t> output) const
{
	/* Encryption or decryption is still running */
	if (sensor_mode.is_crypto_in_progress()) {
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

std::expected<void, ec_host_cmd_status>
FpSensorContext::handle_frame_v1(const ec_params_fp_frame_v1 &params,
				 std::span<uint8_t> output,
				 uint16_t &output_buf_size)
{
	switch (params.cmd) {
	case FP_FRAME_GET_RAW_IMAGE:
		/* The host requested a frame. */
		return frame.get_raw_frame(params.offset, output)
			.transform([&] { output_buf_size = output.size(); })
			.transform_error([](ec_status err) {
				return static_cast<ec_host_cmd_status>(err);
			});
	case FP_FRAME_ENCRYPT_TEMPLATE:
		return start_template_encryption(params.index);
	case FP_FRAME_GET_ENCRYPTED_TEMPLATE:
		return get_encrypted_template_chunk(params.offset, output)
			.transform([&] { output_buf_size = output.size(); });
	}

	return {};
}

std::expected<void, ec_status> FpSensorContext::load_template_chunk(
	uint32_t offset, std::span<const uint8_t> chunk, bool commit)
{
	/* Encryption or decryption is in progress. */
	if (sensor_mode.is_crypto_in_progress()) {
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

	if (commit) {
		return commit_template();
	}

	return {};
}

std::expected<void, ec_status> FpSensorContext::commit_template()
{
	return templates.commit(crypto.user_id.bytes(),
				crypto.tpm_seed.bytes());
}

std::expected<void, ec_status> FpSensorContext::start_template_decryption()
{
	/* Encryption or decryption is in progress. */
	if (sensor_mode.is_crypto_in_progress()) {
		return std::unexpected(EC_RES_BUSY);
	}

	/* Start template decryption. */
	if (!set_sensor_mode(FP_MODE_DECRYPT_TEMPLATE).has_value()) {
		templates.set_decryption_result(EC_RES_ERROR);
		return std::unexpected(EC_RES_ERROR);
	}

	templates.set_decryption_result(EC_RES_BUSY);
	return {};
}

ec_status FpSensorContext::get_template_decryption_result() const noexcept
{
	/* Decryption is still running */
	if (sensor_mode.is_decrypt_template()) {
		return EC_RES_BUSY;
	}

	return templates.get_decryption_result();
}

std::expected<void, ec_status>
FpSensorContext::handle_template_v1(const ec_params_fp_template_v1 &params,
				    uint32_t params_size)
{
	switch (params.cmd) {
	case FP_TEMPLATE_LOAD:
		if (params_size !=
		    params.size + offsetof(ec_params_fp_template_v1, data)) {
			return std::unexpected(EC_RES_INVALID_PARAM);
		}
		return load_template_chunk(params.offset,
					   std::span(params.data, params.size));
	case FP_TEMPLATE_DECRYPT:
		return start_template_decryption();
	case FP_TEMPLATE_GET_RESULT:
		if (const ec_status res = get_template_decryption_result();
		    res != EC_RES_SUCCESS) {
			return std::unexpected(res);
		}
		return {};
	}

	return std::unexpected(EC_RES_INVALID_PARAM);
}

void FpSensorContext::decrypt_and_commit_template()
{
	templates.set_decryption_result(
		commit_template().error_or(EC_RES_SUCCESS));
}

std::expected<void, ec_status>
FpSensorContext::confirm_template(std::span<const uint8_t, FP_MAC_LENGTH> mac)
{
	/*
	 * After successful enrollment, the 'template_newly_enrolled' (new
	 * template id) must be equal to 'templ_valid'.
	 */
	if (templates.template_newly_enrolled != templates.next_free_id()) {
		return std::unexpected(EC_RES_ERROR);
	}

	return crypto
		.validate_request(crypto.user_id.auth_context(),
				  "enroll_finish", mac)
		.transform([this] {
			/* Add newly enrolled template to valid templates. */
			templates.templ_valid++;
		})
		.transform_error(
			[](ec_error_list) { return EC_RES_ACCESS_DENIED; });
}

std::expected<FpSensorMode, ec_error_list>
FpSensorMode::create(uint32_t raw_mode, const FpSensorContext &ctx)
{
	const auto mode = create(raw_mode);
	if (!mode.has_value()) {
		return std::unexpected(mode.error());
	}

	if (mode->is_enroll_session() &&
	    !ctx.templates.next_free_id().has_value()) {
		CPRINTS("Maximum number of fingers already enrolled: %d",
			FP_MAX_FINGER_COUNT);
		return std::unexpected(EC_ERROR_INVAL);
	}

	/* Don't allow sensor reset if any other mode is
	 * set (including FP_MODE_RESET_SENSOR itself).
	 */
	if (mode->is_reset_sensor() && ctx.sensor_mode.is_active()) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	/*
	 * Reject mode changes while a crypto operation is in
	 * progress.
	 */
	if (!mode->is_dont_change() &&
	    ctx.sensor_mode.is_crypto_in_progress()) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	return *mode;
}

std::expected<void, ec_error_list> FpSensorMode::authenticate(
	FpSensorContext &ctx,
	std::optional<std::span<const uint8_t, SHA256_DIGEST_LENGTH> > mac) const
{
	/* Allow for classic fingerprint flow. */
	if (!ctx.crypto.auth_enabled()) {
		return {};
	}

	const auto op = required_auth_operation(ctx.sensor_mode);
	if (!op.has_value()) {
		return std::unexpected(op.error());
	}
	if (!op->has_value()) {
		return {};
	}

	/* Block if the MAC is not available */
	if (!mac.has_value()) {
		return std::unexpected(EC_ERROR_ACCESS_DENIED);
	}

	return ctx.crypto.validate_request(ctx.crypto.user_id.auth_context(),
					   **op, *mac);
}

std::expected<FpSensorMode, ec_status> FpSensorContext::set_sensor_mode(
	uint32_t mode_val,
	std::optional<std::span<const uint8_t, SHA256_DIGEST_LENGTH> > mac)
{
	const auto requested_mode = FpSensorMode::create(mode_val, *this);
	if (!requested_mode.has_value()) {
		CPRINTS("Invalid FP mode 0x%x", mode_val);
		return std::unexpected(EC_RES_INVALID_PARAM);
	}

	if (requested_mode->is_dont_change()) {
		return sensor_mode;
	}

	if (const auto auth = requested_mode->authenticate(*this, mac);
	    !auth.has_value()) {
		CPRINTS("Mode authentication failed with: %d", auth.error());
		return std::unexpected(EC_RES_ACCESS_DENIED);
	}

	sensor_mode = *requested_mode;
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG);
	return sensor_mode;
}

std::expected<void, ec_status>
FpSensorContext::set_context(const ec_params_fp_context_v1 &p)
{
	switch (p.action) {
	case FP_CONTEXT_ASYNC:
		if (sensor_mode.is_reset_sensor()) {
			return std::unexpected(EC_RES_BUSY);
		}

		/**
		 * Trigger a call to fp_reset_and_clear_context() by
		 * requesting a reset. Since that function triggers a call to
		 * fp_sensor_open(), this must be asynchronous because
		 * fp_sensor_open() can take ~175 ms. See http://b/137288498.
		 */
		return set_sensor_mode(FP_MODE_RESET_SENSOR)
			.transform([](FpSensorMode) {});

	case FP_CONTEXT_GET_RESULT:
		if (sensor_mode.is_reset_sensor()) {
			return std::unexpected(EC_RES_BUSY);
		}

		crypto.user_id.set(p.userid);

		return {};
	}

	return std::unexpected(EC_RES_INVALID_PARAM);
}

std::expected<FpPositiveMatchSecret, ec_status>
FpSensorContext::read_match_secret(int8_t fgr)
{
	return templates.read_match_secret(fgr, crypto.user_id.bytes(),
					   crypto.tpm_seed.bytes());
}

std::expected<std::array<uint8_t, FP_MAC_LENGTH>, ec_status>
FpSensorContext::sign_match(
	std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge)
{
	if (!crypto.auth_enabled()) {
		return std::unexpected(EC_RES_ACCESS_DENIED);
	}

	if (const auto matched =
		    templates.positive_match_secret_state.consume();
	    !matched.has_value()) {
		if (matched.error() == EC_RES_TIMEOUT) {
			CPRINTS("Deadline has passed");
		} else {
			CPRINTS("No match to be signed");
		}
		return std::unexpected(matched.error());
	}

	return crypto
		.sign_message(crypto.user_id.auth_context(), "auth", challenge)
		.transform_error([](ec_error_list) {
			CPRINTS("Failed to sign message");
			return EC_RES_ERROR;
		});
}
