/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "common.h"
#include "compile_time_macros.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_image_frame_params.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor_driver.h"
#include "fpsensor_matcher.h"
#include "host_command.h"
#include "openssl/mem.h"
#include "system.h"
#include "task.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <bit>
#include <variant>

/* Last acquired frame (aligned as it is used by arbitrary binary libraries) */
uint8_t fp_buffer[FP_SENSOR_IMAGE_SIZE] FP_FRAME_SECTION __aligned(4);
/* Fingers templates for the current user */
test_mockable std::array<FpTemplate, FP_MAX_FINGER_COUNT>
	fp_template FP_TEMPLATE_SECTION __aligned(4);
static_assert(
	sizeof(fp_template[0]) % 4 == 0,
	"The size of each template must be a multiple of 4 to ensure that the next "
	"template will still be aligned by 4.");

/* Encryption/decryption buffer */
/* TODO: On-the-fly encryption/decryption without a dedicated buffer */
/*
 * Store the encryption metadata at the beginning of the buffer containing the
 * ciphered data.
 */
enc_buffer fp_enc_buffer FP_TEMPLATE_SECTION;

fpsensor_context global_context;

void cleanse(std::span<uint8_t> bytes) noexcept
{
	OPENSSL_cleanse(bytes.data(), bytes.size_bytes());
}

/* LCOV_EXCL_START */
__test_only void fp_task_simulate(void)
{
	int timeout_us = -1;

	while (1)
		task_wait_event(timeout_us);
}
/* LCOV_EXCL_STOP */

void enc_buffer::clear() noexcept
{
	cleanse(as_bytes());
}

void FpTemplateStore::clear_finger(FpTemplateId id) noexcept
{
	cleanse(templates[id.raw()]);
	cleanse(fp_positive_match_salt[id.raw()]);
}

void FpTemplateStore::reset() noexcept
{
	templ_valid = 0;
	templ_dirty.reset();
	template_newly_enrolled.reset();
	template_encrypted_id.reset();
	enc_buf->clear();
	positive_match_secret_state.disable();
	std::ranges::for_each(templates, [](auto &t) { cleanse(t); });
	std::ranges::for_each(fp_positive_match_salt,
			      [](auto &s) { cleanse(s); });
}

void FpCryptoContext::reset_user_context() noexcept
{
	encrypted_template_ready = false;
	active_challenge.reset();
	cleanse(user_id);
}

void fpsensor_context::reset_context() noexcept
{
	templates.reset();
	crypto.reset_user_context();
}

/**
 * @warning |frame.image_buffer| contains data used by the matching algorithm
 * that must be released by calling fp_sensor_deinit() first. Call
 * reset_and_clear_context() instead of calling this directly.
 */
void fpsensor_context::clear_context() noexcept
{
	reset_context();
	frame.clear();
}

void fpsensor_context::reset_and_clear_context()
{
	if (fp_sensor_deinit() != EC_SUCCESS)
		CPRINTS("Failed to deinit sensor");
	clear_context();
	if (fp_sensor_init() != EC_SUCCESS)
		CPRINTS("Failed to init sensor");
}

int fp_get_next_event(uint8_t *out)
{
	const uint32_t event_out = global_context.take_events();

	std::ranges::copy(
		std::bit_cast<std::array<uint8_t, sizeof(event_out)> >(
			event_out),
		out);

	return sizeof(event_out);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_FINGERPRINT, fp_get_next_event);

std::expected<void, ec_status>
FpCryptoContext::set_tpm_seed(const ec_params_fp_seed &params)
{
	if (params.struct_version != FP_TEMPLATE_FORMAT_VERSION) {
		CPRINTS("Invalid seed format %d", params.struct_version);
		return std::unexpected(EC_RES_INVALID_PARAM);
	}

	if (tpm_seed_set()) {
		CPRINTS("Seed has already been set.");
		return std::unexpected(EC_RES_ACCESS_DENIED);
	}
	set_tpm_seed(params.seed);

	return {};
}

static ec_status fp_command_tpm_seed(host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_seed *>(args->params);

	return global_context.crypto.set_tpm_seed(*params).error_or(
		EC_RES_SUCCESS);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_SEED, fp_command_tpm_seed, EC_VER_MASK(0));

static ec_status fp_command_encryption_status(host_cmd_handler_args *args)
{
	auto *r =
		static_cast<ec_response_fp_encryption_status *>(args->response);

	r->valid_flags = FP_ENC_STATUS_SEED_SET;
	r->status = global_context.crypto.encryption_status();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ENC_STATUS, fp_command_encryption_status,
		     EC_VER_MASK(0));

std::expected<FpSensorMode, ec_error_list>
FpSensorMode::create(uint32_t raw_mode, const fpsensor_context &ctx)
{
	return create(raw_mode).and_then([&ctx](FpSensorMode mode)
						 -> std::expected<FpSensorMode,
								  ec_error_list> {
		if (mode.has_any(FP_MODE_ENROLL_SESSION) &&
		    !ctx.templates.next_free_id().has_value()) {
			CPRINTS("Maximum number of fingers already enrolled: %d",
				FP_MAX_FINGER_COUNT);
			return std::unexpected(EC_ERROR_INVAL);
		}

		/* Don't allow sensor reset if any other mode is
		 * set (including FP_MODE_RESET_SENSOR itself).
		 */
		if (mode.has_any(FP_MODE_RESET_SENSOR) &&
		    ctx.has_any_mode(FP_VALID_MODES)) {
			return std::unexpected(EC_ERROR_INVAL);
		}

		/*
		 * Reject mode changes while a crypto operation is in
		 * progress.
		 */
		if (!mode.has_any(FP_MODE_DONT_CHANGE) &&
		    ctx.is_crypto_in_progress()) {
			return std::unexpected(EC_ERROR_INVAL);
		}

		return mode;
	});
}

static std::expected<void, ec_error_list> authenticate_fp_mode(
	const FpSensorMode mode, const fpsensor_context &ctx,
	std::optional<std::span<const uint8_t, SHA256_DIGEST_LENGTH> > mac)
{
	/* Allow for classic fingerprint flow. */
	if (!ctx.crypto.auth_enabled()) {
		return {};
	}

	/* Modes that don't require authentication are allowed. */
	if (!mode.enables_any(FP_MODES_WITH_AUTHENTICATION, ctx.mode())) {
		return {};
	}

	/*
	 * Reject requests that would result in multiple auth-gated bits being
	 * set. A single cryptographic MAC is tied to exactly one operation
	 * string.
	 */
	if (mode.count(FP_MODES_WITH_AUTHENTICATION) > 1) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	/* Block if the MAC is not available */
	if (!mac.has_value()) {
		return std::unexpected(EC_ERROR_ACCESS_DENIED);
	}

	const std::string_view operation =
		mode.enables_any(FP_MODE_ENROLL_SESSION, ctx.mode()) ?
			"enroll" :
			"auth";
	return validate_request(ctx.crypto.auth_user_id_context(), operation,
				*mac);
}

std::expected<FpSensorMode, ec_status> fp_set_sensor_mode(
	uint32_t mode,
	std::optional<std::span<const uint8_t, SHA256_DIGEST_LENGTH> > mac,
	fpsensor_context &ctx)
{
	return FpSensorMode::create(mode, ctx)
		.or_else([mode](ec_error_list)
				 -> std::expected<FpSensorMode, ec_status> {
			CPRINTS("Invalid FP mode 0x%x", mode);
			return std::unexpected(EC_RES_INVALID_PARAM);
		})
		.and_then([&ctx, mac](FpSensorMode requested_mode)
				  -> std::expected<FpSensorMode, ec_status> {
			if (requested_mode.has_any(FP_MODE_DONT_CHANGE)) {
				return ctx.mode();
			}
			return authenticate_fp_mode(requested_mode, ctx, mac)
				.or_else([](ec_error_list err)
						 -> std::expected<void,
								  ec_status> {
					CPRINTS("Mode authentication failed with: %d",
						err);
					return std::unexpected(
						EC_RES_ACCESS_DENIED);
				})
				.transform([&ctx, requested_mode]() {
					ctx.sensor_mode = requested_mode.raw();
					task_set_event(
						TASK_ID_FPSENSOR,
						TASK_EVENT_UPDATE_CONFIG);
					return ctx.mode();
				});
		});
}

static ec_status fp_command_mode(host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_mode *>(args->params);
	auto *r = static_cast<ec_response_fp_mode *>(args->response);
	const auto mac =
		(args->version == 1) ?
			std::optional<std::span<const uint8_t, FP_MAC_LENGTH> >{
				static_cast<const ec_params_fp_mode_v1 *>(
					args->params)
					->mac
			} :
			std::nullopt;

	return fp_set_sensor_mode(p->mode, mac)
		.transform([args, r](FpSensorMode mode) {
			r->mode = mode.raw();
			args->response_size = sizeof(*r);
		})
		.error_or(EC_RES_SUCCESS);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_MODE, fp_command_mode,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

std::expected<void, ec_status>
fpsensor_context::set_context(const ec_params_fp_context_v1 &p)
{
	switch (p.action) {
	case FP_CONTEXT_ASYNC:
		if (has_any_mode(FP_MODE_RESET_SENSOR))
			return std::unexpected(EC_RES_BUSY);

		/**
		 * Trigger a call to fp_reset_and_clear_context() by
		 * requesting a reset. Since that function triggers a call to
		 * fp_sensor_open(), this must be asynchronous because
		 * fp_sensor_open() can take ~175 ms. See http://b/137288498.
		 */
		return fp_set_sensor_mode(FP_MODE_RESET_SENSOR, std::nullopt,
					  *this)
			.transform([](FpSensorMode) {});

	case FP_CONTEXT_GET_RESULT:
		if (has_any_mode(FP_MODE_RESET_SENSOR))
			return std::unexpected(EC_RES_BUSY);

		crypto.set_user_id(p.userid);

		return {};
	}

	return std::unexpected(EC_RES_INVALID_PARAM);
}

static ec_status fp_command_context(host_cmd_handler_args *args)
{
	const auto *p =
		static_cast<const ec_params_fp_context_v1 *>(args->params);

	return global_context.set_context(*p).error_or(EC_RES_SUCCESS);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_CONTEXT, fp_command_context, EC_VER_MASK(1));

std::expected<void, ec_error_list>
positive_match_secret_state::enable(FpTemplateId fgr)
{
	if (has_any_match()) {
		CPRINTS("Error: positive match secret already readable.");
		disable();
		return std::unexpected(EC_ERROR_UNKNOWN);
	}

	template_matched = fgr;
	deadline = FpClock::now() + kValidityWindow;
	return {};
}

std::expected<
	CleanseWrapper<std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> >,
	ec_status>
FpTemplateStore::read_match_secret(
	int8_t fgr, std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	return FpTemplateId::create(fgr)
		.or_else([this, fgr](ec_status err)
				 -> std::expected<FpTemplateId, ec_status> {
			positive_match_secret_state.disable();
			CPRINTS("Invalid finger number %d", fgr);
			return std::unexpected(err);
		})
		.and_then([this, fgr](FpTemplateId id) {
			return positive_match_secret_state.consume(id).or_else(
				[fgr](ec_status err)
					-> std::expected<FpTemplateId,
							 ec_status> {
					if (err == EC_RES_TIMEOUT) {
						CPRINTS("Reading positive match secret disallowed: "
							"deadline has passed.");
					} else {
						CPRINTS("Positive match secret for finger %d is not meant to "
							"be read now.",
							fgr);
					}
					return std::unexpected(err);
				});
		})
		.and_then([this, fgr, user_id, tpm_seed](FpTemplateId matched) {
			return derive_positive_match_secret(
				       fp_positive_match_salt[matched.raw()],
				       user_id, tpm_seed)
				.transform([fgr](auto secret) {
					CPRINTS("Derived positive match secret for finger %d",
						fgr);
					return secret;
				})
				.or_else([fgr](ec_error_list)
						 -> std::expected<
							 CleanseWrapper<std::array<
								 uint8_t,
								 FP_POSITIVE_MATCH_SECRET_BYTES> >,
							 ec_status> {
					CPRINTS("Failed to derive positive match secret for finger %d",
						fgr);
					/* Keep the template and encryption
					 * salt. */
					return std::unexpected(EC_RES_ERROR);
				});
		});
}

std::expected<
	CleanseWrapper<std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> >,
	ec_status>
fpsensor_context::read_match_secret(int8_t fgr)
{
	return templates.read_match_secret(fgr, crypto.user_id,
					   crypto.tpm_seed);
}

static ec_status fp_command_read_match_secret(host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_read_match_secret *>(
			args->params);
	auto *response =
		static_cast<ec_response_fp_read_match_secret *>(args->response);

	return global_context.read_match_secret(params->fgr)
		.transform([args, response](const auto &secret) {
			std::ranges::copy(secret,
					  response->positive_match_secret);
			args->response_size = sizeof(*response);
		})
		.error_or(EC_RES_SUCCESS);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_READ_MATCH_SECRET, fp_command_read_match_secret,
		     EC_VER_MASK(0));

std::expected<std::array<uint8_t, FP_MAC_LENGTH>, ec_status>
fpsensor_context::sign_match(
	std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge)
{
	if (!crypto.auth_enabled()) {
		return std::unexpected(EC_RES_ACCESS_DENIED);
	}

	return templates.positive_match_secret_state.consume()
		.or_else([](ec_status err)
				 -> std::expected<FpTemplateId, ec_status> {
			if (err == EC_RES_TIMEOUT) {
				CPRINTS("Deadline has passed");
			} else {
				CPRINTS("No match to be signed");
			}
			return std::unexpected(err);
		})
		.and_then([this, challenge](FpTemplateId) {
			return crypto
				.sign_message(crypto.auth_user_id_context(),
					      "auth", challenge)
				.or_else([](ec_error_list)
						 -> std::expected<
							 std::array<uint8_t,
								    FP_MAC_LENGTH>,
							 ec_status> {
					CPRINTS("Failed to sign message");
					return std::unexpected(EC_RES_ERROR);
				});
		});
}

static ec_status fp_command_sign_match(host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_sign_match *>(args->params);
	auto *response =
		static_cast<ec_response_fp_sign_match *>(args->response);

	return global_context.sign_match(params->challenge)
		.transform([args, response](const auto &signature) {
			std::ranges::copy(signature, response->signature);
			args->response_size = sizeof(*response);
		})
		.error_or(EC_RES_SUCCESS);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_SIGN_MATCH, fp_command_sign_match,
		     EC_VER_MASK(0));
