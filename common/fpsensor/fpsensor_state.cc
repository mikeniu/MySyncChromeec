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

#ifdef CONFIG_ZEPHYR
#include <zephyr/shell/shell.h>
#endif

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

void fp_clear_finger_context(FpTemplateId id)
{
	cleanse(fp_template[id.raw()]);
	cleanse(global_context.fp_positive_match_salt[id.raw()]);
}

void fp_reset_context()
{
	global_context.templ_valid = 0;
	global_context.templ_dirty = 0;
	global_context.template_newly_enrolled.reset();
	global_context.fp_encryption_status &=
		FP_ENC_STATUS_SEED_SET | FP_CONTEXT_SESSION_NONCE_SET |
		FP_CONTEXT_STATUS_SESSION_ESTABLISHED;
	fp_enc_buffer.clear();
	global_context.clear_encrypted_template();
	cleanse(global_context.user_id);
	global_context.positive_match_secret_state.disable();
	for (uint16_t idx = 0; idx < FP_MAX_FINGER_COUNT; idx++)
		fp_clear_finger_context(*FpTemplateId::create(idx));
}

/**
 * @warning |fp_buffer| contains data used by the matching algorithm that must
 * be released by calling fp_sensor_deinit() first. Call
 * fp_reset_and_clear_context instead of calling this directly.
 */
static void _fp_clear_context()
{
	fp_reset_context();
	cleanse(fp_buffer);
	/* Reset capture type, as it is correlated with fp_buffer. */
	global_context.current_capture_type.reset();
}

void fp_reset_and_clear_context()
{
	if (fp_sensor_deinit() != EC_SUCCESS)
		CPRINTS("Failed to deinit sensor");
	_fp_clear_context();
	if (fp_sensor_init() != EC_SUCCESS)
		CPRINTS("Failed to init sensor");
}

int fp_get_next_event(uint8_t *out)
{
	uint32_t event_out = global_context.take_events();

	memcpy(out, &event_out, sizeof(event_out));

	return sizeof(event_out);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_FINGERPRINT, fp_get_next_event);

static ec_status fp_command_tpm_seed(host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_seed *>(args->params);

	if (params->struct_version != FP_TEMPLATE_FORMAT_VERSION) {
		CPRINTS("Invalid seed format %d", params->struct_version);
		return EC_RES_INVALID_PARAM;
	}

	if (global_context.tpm_seed_set()) {
		CPRINTS("Seed has already been set.");
		return EC_RES_ACCESS_DENIED;
	}
	global_context.set_tpm_seed(params->seed);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_SEED, fp_command_tpm_seed, EC_VER_MASK(0));

static ec_status fp_command_encryption_status(host_cmd_handler_args *args)
{
	auto *r =
		static_cast<ec_response_fp_encryption_status *>(args->response);

	r->valid_flags = FP_ENC_STATUS_SEED_SET;
	r->status = global_context.fp_encryption_status;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ENC_STATUS, fp_command_encryption_status,
		     EC_VER_MASK(0));

std::expected<FpSensorMode, ec_error_list>
FpSensorMode::create(uint32_t raw_mode, const fpsensor_context &ctx)
{
	auto mode = create(raw_mode);
	if (!mode.has_value()) {
		return mode;
	}

	if (mode->has_any(FP_MODE_ENROLL_SESSION) &&
	    !ctx.next_free_template_id().has_value()) {
		CPRINTS("Maximum number of fingers already enrolled: %d",
			FP_MAX_FINGER_COUNT);
		return std::unexpected(EC_ERROR_INVAL);
	}

	/* Don't allow sensor reset if any other mode is
	 * set (including FP_MODE_RESET_SENSOR itself).
	 */
	if (mode->has_any(FP_MODE_RESET_SENSOR) &&
	    ctx.has_any_mode(FP_VALID_MODES)) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	/*
	 * Reject mode changes while a crypto operation is in progress.
	 */
	if (!mode->has_any(FP_MODE_DONT_CHANGE) &&
	    ctx.is_crypto_in_progress()) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	return mode;
}

static std::expected<void, ec_error_list>
authenticate_enroll(std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac)
{
	return validate_request(global_context.auth_user_id_context(),
				as_operation_span("enroll"), mac);
}

static std::expected<void, ec_error_list>
authenticate_match(std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac)
{
	return validate_request(global_context.auth_user_id_context(),
				as_operation_span("auth"), mac);
}

static std::expected<void, ec_error_list> authenticate_fp_mode(
	const FpSensorMode mode,
	std::optional<std::span<const uint8_t, SHA256_DIGEST_LENGTH> > mac)
{
	/* Allow for classic fingerprint flow. */
	if (!fingerprint_auth_enabled()) {
		return {};
	}

	/* Modes that don't require authentication are allowed. */
	if (!mode.enables_any(FP_MODES_WITH_AUTHENTICATION,
			      global_context.sensor_mode)) {
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

	if (mode.enables_any(FP_MODE_ENROLL_SESSION,
			     global_context.sensor_mode)) {
		return authenticate_enroll(mac.value());
	}
	return authenticate_match(mac.value());
}

std::expected<FpSensorMode, ec_status> fp_set_sensor_mode(
	uint32_t mode,
	std::optional<std::span<const uint8_t, SHA256_DIGEST_LENGTH> > mac)
{
	const auto requested_mode = FpSensorMode::create(mode, global_context);
	if (!requested_mode.has_value()) {
		CPRINTS("Invalid FP mode 0x%x", mode);
		return std::unexpected(EC_RES_INVALID_PARAM);
	}

	if (!requested_mode->has_any(FP_MODE_DONT_CHANGE)) {
		const auto auth = authenticate_fp_mode(*requested_mode, mac);
		if (!auth.has_value()) {
			CPRINTS("Mode authentication failed with: %d",
				auth.error());
			return std::unexpected(EC_RES_ACCESS_DENIED);
		}

		global_context.sensor_mode = requested_mode->raw();
		task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG);
	}

	return FpSensorMode(global_context.sensor_mode);
}

static ec_status fp_command_mode(host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_mode *>(args->params);
	const auto *p_1 =
		static_cast<const ec_params_fp_mode_v1 *>(args->params);
	auto *r = static_cast<ec_response_fp_mode *>(args->response);
	std::optional<std::span<const uint8_t, FP_MAC_LENGTH> > mac =
		std::nullopt;

	if (args->version == 1) {
		mac = std::span{ p_1->mac };
	}

	const auto result = fp_set_sensor_mode(p->mode, mac);
	if (!result.has_value()) {
		return result.error();
	}

	r->mode = result->raw();
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_MODE, fp_command_mode,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static ec_status fp_command_context(host_cmd_handler_args *args)
{
	const auto *p =
		static_cast<const ec_params_fp_context_v1 *>(args->params);

	switch (p->action) {
	case FP_CONTEXT_ASYNC:
		if (global_context.has_any_mode(FP_MODE_RESET_SENSOR))
			return EC_RES_BUSY;

		/**
		 * Trigger a call to fp_reset_and_clear_context() by
		 * requesting a reset. Since that function triggers a call to
		 * fp_sensor_open(), this must be asynchronous because
		 * fp_sensor_open() can take ~175 ms. See http://b/137288498.
		 */
		return fp_set_sensor_mode(FP_MODE_RESET_SENSOR)
			.error_or(EC_RES_SUCCESS);

	case FP_CONTEXT_GET_RESULT:
		if (global_context.has_any_mode(FP_MODE_RESET_SENSOR))
			return EC_RES_BUSY;

		global_context.set_user_id(
			std::span<const uint8_t, FP_CONTEXT_USERID_BYTES>(
				reinterpret_cast<const uint8_t *>(p->userid),
				sizeof(p->userid)));

		return EC_RES_SUCCESS;
	}

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_CONTEXT, fp_command_context, EC_VER_MASK(1));

ec_error_list positive_match_secret_state::enable(FpTemplateId fgr)
{
	if (has_any_match()) {
		CPRINTS("Error: positive match secret already readable.");
		disable();
		return EC_ERROR_UNKNOWN;
	}

	template_matched = fgr;
	deadline.val = get_time().val + (15 * SECOND); /* b/478160541 */
	return EC_SUCCESS;
}

std::expected<
	CleanseWrapper<std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> >,
	ec_status>
fp_read_match_secret(int8_t fgr)
{
	const auto id = FpTemplateId::create(fgr);
	if (!id.has_value()) {
		global_context.positive_match_secret_state.disable();
		CPRINTS("Invalid finger number %d", fgr);
		return std::unexpected(id.error());
	}
	const auto matched =
		global_context.positive_match_secret_state.consume(*id);
	if (!matched.has_value()) {
		if (matched.error() == EC_RES_TIMEOUT) {
			CPRINTS("Reading positive match secret disallowed: "
				"deadline has passed.");
		} else {
			CPRINTS("Positive match secret for finger %d is not meant to "
				"be read now.",
				fgr);
		}
		return std::unexpected(matched.error());
	}

	auto secret = derive_positive_match_secret(
		global_context.fp_positive_match_salt[matched->raw()],
		global_context.user_id, global_context.tpm_seed);
	if (!secret.has_value()) {
		CPRINTS("Failed to derive positive match secret for finger %d",
			fgr);
		/* Keep the template and encryption salt. */
		return std::unexpected(EC_RES_ERROR);
	}
	CPRINTS("Derived positive match secret for finger %d", fgr);

	return *secret;
}

static ec_status fp_command_read_match_secret(host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_read_match_secret *>(
			args->params);
	auto *response =
		static_cast<ec_response_fp_read_match_secret *>(args->response);

	const auto secret = fp_read_match_secret(params->fgr);
	if (!secret.has_value()) {
		return secret.error();
	}

	std::ranges::copy(*secret, response->positive_match_secret);
	args->response_size = sizeof(*response);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_READ_MATCH_SECRET, fp_command_read_match_secret,
		     EC_VER_MASK(0));

static ec_status fp_command_sign_match(host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_sign_match *>(args->params);
	auto *response =
		static_cast<ec_response_fp_sign_match *>(args->response);

	std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge{
		params->challenge
	};

	if (!fingerprint_auth_enabled()) {
		return EC_RES_ACCESS_DENIED;
	}

	if (const auto matched =
		    global_context.positive_match_secret_state.consume();
	    !matched.has_value()) {
		if (matched.error() == EC_RES_TIMEOUT) {
			CPRINTS("Deadline has passed");
		} else {
			CPRINTS("No match to be signed");
		}
		return matched.error();
	}

	const auto signature =
		sign_message(global_context.auth_user_id_context(),
			     as_operation_span("auth"), challenge);
	if (!signature.has_value()) {
		CPRINTS("Failed to sign message");
		return EC_RES_ERROR;
	}

	std::ranges::copy(*signature, response->signature);
	args->response_size = sizeof(*response);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_SIGN_MATCH, fp_command_sign_match,
		     EC_VER_MASK(0));
