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

#pragma GCC poison global_context

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
FpEncryptedTemplateBuffer fp_enc_buffer FP_TEMPLATE_SECTION;

void cleanse(std::span<uint8_t> bytes) noexcept
{
	OPENSSL_cleanse(bytes.data(), bytes.size_bytes());
}

/* LCOV_EXCL_START */
__test_only void fp_task_simulate()
{
	constexpr int kTimeoutUs = -1;

	while (true) {
		task_wait_event(kTimeoutUs);
	}
}
/* LCOV_EXCL_STOP */

void FpEncryptedTemplateBuffer::clear() noexcept
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
	user_id.clear();
}

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

std::expected<void, ec_status>
FpTpmSeed::set_from_host(const ec_params_fp_seed &params)
{
	if (params.struct_version != FP_TEMPLATE_FORMAT_VERSION) {
		CPRINTS("Invalid seed format %d", params.struct_version);
		return std::unexpected(EC_RES_INVALID_PARAM);
	}

	if (is_set()) {
		CPRINTS("Seed has already been set.");
		return std::unexpected(EC_RES_ACCESS_DENIED);
	}
	set(params.seed);

	return {};
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

std::expected<void, ec_error_list>
FpPositiveMatchSecretState::enable(FpTemplateId fgr)
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

std::expected<FpPositiveMatchSecret, ec_status>
FpTemplateStore::read_match_secret(
	int8_t fgr, std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	const auto id = FpTemplateId::create(fgr);
	if (!id.has_value()) {
		positive_match_secret_state.disable();
		CPRINTS("Invalid finger number %d", fgr);
		return std::unexpected(id.error());
	}

	const auto matched = positive_match_secret_state.consume(*id);
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

	const auto secret = FpPositiveMatchSecret::derive(
		fp_positive_match_salt[matched->raw()], user_id, tpm_seed);
	if (!secret.has_value()) {
		CPRINTS("Failed to derive positive match secret for finger %d",
			fgr);
		/* Keep the template and encryption salt. */
		return std::unexpected(EC_RES_ERROR);
	}

	CPRINTS("Derived positive match secret for finger %d", fgr);
	return *secret;
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
