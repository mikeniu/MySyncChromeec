/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_state.h"
#include "openssl/mem.h"
#include "openssl/rand.h"
#include "scoped_fast_cpu.h"
#include "sha256.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#ifdef CONFIG_PLATFORM_EC_WP_EXTERNAL
#include <wp_external.h>
#endif

/* The session nonce for session key. */
static std::array<uint8_t, FP_CK_SESSION_NONCE_LEN> session_nonce;

static std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key;

/* Current challenge */
test_export_static timestamp_t challenge_ctime;

namespace
{

struct AuthChallenge {
	static constexpr std::chrono::seconds kExpiry{ 5 };

	std::array<uint8_t, FP_CHALLENGE_SIZE> bytes{};

	void generate(std::span<uint8_t, FP_CHALLENGE_SIZE> out)
	{
		RAND_bytes(bytes.data(), bytes.size());
		std::ranges::copy(bytes, out.begin());
		challenge_ctime = get_time();
		global_context.fp_encryption_status |= FP_AUTH_CHALLENGE_SET;
	}

	[[nodiscard]] std::expected<void, ec_error_list>
	consume_and_validate_age(timestamp_t now = get_time()) noexcept
	{
		if (!(global_context.fp_encryption_status &
		      FP_AUTH_CHALLENGE_SET)) {
			return std::unexpected(EC_ERROR_ACCESS_DENIED);
		}
		global_context.fp_encryption_status &= ~FP_AUTH_CHALLENGE_SET;
		if (now.val > (challenge_ctime + kExpiry).val) {
			return std::unexpected(EC_ERROR_TIMEOUT);
		}
		return {};
	}
};

AuthChallenge active_challenge;

} /* namespace */

bool fingerprint_auth_enabled()
{
	return global_context.fp_encryption_status &
	       FP_CONTEXT_STATUS_SESSION_ESTABLISHED;
}

__maybe_unused test_export_static void reset_session(void)
{
	cleanse(session_nonce);
	cleanse(session_key);
	global_context.clear_tpm_seed();
	global_context.fp_encryption_status &=
		~(FP_CONTEXT_SESSION_NONCE_SET |
		  FP_CONTEXT_STATUS_SESSION_ESTABLISHED);
}

static enum ec_status
fp_command_generate_nonce(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_generate_nonce *>(args->response);

	ScopedFastCpu fast_cpu;

	RAND_bytes(session_nonce.data(), session_nonce.size());

	std::ranges::copy(session_nonce, r->nonce);

	global_context.fp_encryption_status |= FP_CONTEXT_SESSION_NONCE_SET;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_GENERATE_NONCE, fp_command_generate_nonce,
		     EC_VER_MASK(0));

static enum ec_status
fp_command_establish_session(struct host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_establish_session *>(
		args->params);

	if (!(global_context.fp_encryption_status &
	      FP_CONTEXT_SESSION_NONCE_SET)) {
		CPRINTS("No existing session nonce");
		return EC_RES_ACCESS_DENIED;
	}

	/* Do not establish session if any operation on templates is running */
	if (global_context.has_any_mode(FP_MODES_TEMPLATE_OPERATION)) {
		return EC_RES_BUSY;
	}

	ScopedFastCpu fast_cpu;

	/* Avoid using old TPM Seed if the seed decryption fails. */
	global_context.clear_tpm_seed();

	/* Invalidate the existing context and templates to prevent
	 * adding new templates unauthorized by TA.
	 */
	fp_reset_context();

	/* Reset SESSION_ESTABLISHED bit to avoid situation that the
	 * Fingerprint Auth is enabled with TPM Seed set using
	 * EC_CMD_FP_SEED_SET command (which is possible when the TPM Seed is
	 * not set, e.g. due to seed decryption error).
	 */
	global_context.fp_encryption_status &=
		~FP_CONTEXT_STATUS_SESSION_ESTABLISHED;

	const auto new_session_key =
		generate_session_key_with_context(session_nonce, p->peer_nonce);
	if (!new_session_key.has_value()) {
		return EC_RES_INVALID_PARAM;
	}
	session_key = *new_session_key;

	static_assert(sizeof(global_context.tpm_seed) ==
		      sizeof(p->enc_tpm_seed));
	CleanseWrapper<std::array<uint8_t, sizeof(global_context.tpm_seed)> >
		tpm_seed;

	if (!decrypt_data_with_session_key(session_key, p->enc_tpm_seed,
					   tpm_seed, p->nonce, p->tag,
					   as_operation_span("tpm_seed"))
		     .has_value()) {
		return EC_RES_ERROR;
	}

	/* Set the TPM Seed. */
	global_context.set_tpm_seed(tpm_seed);
	global_context.fp_encryption_status &= ~FP_CONTEXT_SESSION_NONCE_SET;
	global_context.fp_encryption_status |=
		FP_CONTEXT_STATUS_SESSION_ESTABLISHED;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ESTABLISH_SESSION, fp_command_establish_session,
		     EC_VER_MASK(0));

static enum ec_status
fp_cmd_generate_challenge(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_generate_challenge *>(
		args->response);

	/* The Session Key is used to sign messages. Let's make sure
	 * it's available. */
	if (!fingerprint_auth_enabled()) {
		return EC_RES_ACCESS_DENIED;
	}

	ScopedFastCpu fast_cpu;

	active_challenge.generate(r->challenge);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_GENERATE_CHALLENGE, fp_cmd_generate_challenge,
		     EC_VER_MASK(0));

test_mockable std::expected<void, ec_error_list>
validate_request(std::span<const uint8_t> context,
		 std::span<const uint8_t> operation,
		 std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac)
{
	if (const auto res = active_challenge.consume_and_validate_age();
	    !res.has_value()) {
		return res;
	}

	/* We expect the message to come from Fingerguard. */
	const auto computed_mac = compute_message_signature(
		session_key, context, as_operation_span("finger_guard"),
		operation, active_challenge.bytes);
	if (!computed_mac.has_value()) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	/* Compare computed signature with received one. */
	static_assert(mac.size() == sizeof(*computed_mac));
	if (CRYPTO_memcmp(mac.data(), computed_mac->data(), mac.size())) {
		return std::unexpected(EC_ERROR_ACCESS_DENIED);
	}

	return {};
}

std::expected<std::array<uint8_t, SHA256_DIGEST_LENGTH>, ec_error_list>
sign_message(std::span<const uint8_t> context,
	     std::span<const uint8_t> operation,
	     std::span<const uint8_t, FP_CHALLENGE_SIZE> peer_challenge)
{
	/* The Session Key is used to sign messages. Let's make sure
	 * it's available. */
	if (!fingerprint_auth_enabled()) {
		return std::unexpected(EC_ERROR_ACCESS_DENIED);
	}

	return compute_message_signature(session_key, context,
					 as_operation_span("fpmcu"), operation,
					 peer_challenge);
}

#ifdef CONFIG_PLATFORM_EC_WP_EXTERNAL
static enum ec_status
fp_command_unlock_dev_options(struct host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_unlock_dev_options *>(
		args->params);

	/* Validate with an empty context (no user_id). */
	if (!validate_request({}, as_operation_span("enable_dev_options"),
			      p->hmac)
		     .has_value()) {
		return EC_RES_ACCESS_DENIED;
	}

	/* Unlock the system */
	disable_write_protect_external();

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_UNLOCK_DEV_OPTIONS,
		     fp_command_unlock_dev_options, EC_VER_MASK(0));
#endif
