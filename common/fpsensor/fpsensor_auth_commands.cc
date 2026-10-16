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

void FpCryptoContext::reset_session() noexcept
{
	clear_session_nonce();
	cleanse(session_key);
	clear_tpm_seed();
	set_auth_enabled(false);
}

void FpCryptoContext::generate_nonce(
	std::span<uint8_t, FP_CK_SESSION_NONCE_LEN> out)
{
	auto &nonce = session_nonce.emplace();
	RAND_bytes(nonce.data(), nonce.size());
	std::ranges::copy(nonce, out.begin());
}

void FpCryptoContext::generate_challenge(
	std::span<uint8_t, FP_CHALLENGE_SIZE> out)
{
	FpAuthChallenge challenge{
		.value = {},
		.deadline = FpClock::now() + kChallengeExpiry,
	};
	RAND_bytes(challenge.value.data(), challenge.value.size());
	std::ranges::copy(challenge.value, out.begin());
	active_challenge = challenge;
}

std::expected<FpAuthChallenge, ec_error_list>
FpCryptoContext::consume_challenge(FpTimestamp now) noexcept
{
	const auto challenge = std::exchange(active_challenge, std::nullopt);
	if (!challenge.has_value()) {
		return std::unexpected(EC_ERROR_ACCESS_DENIED);
	}
	if (challenge->is_expired(now)) {
		return std::unexpected(EC_ERROR_TIMEOUT);
	}
	return *challenge;
}

std::expected<CleanseWrapper<std::array<uint8_t, SHA256_DIGEST_LENGTH> >,
	      ec_error_list>
FpCryptoContext::derive_session_key(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce) const
{
#ifdef CONFIG_PLATFORM_EC_FINGERPRINT_AUTH_METHOD_ASCP
	const auto context = as_operation_span("sdcp");
#else
	const std::span<const uint8_t> context{};
#endif
	return generate_session_key(*session_nonce, peer_nonce, pairing_key,
				    context);
}

std::expected<void, ec_error_list>
FpCryptoContext::validate_request(std::span<const uint8_t> context,
				  std::span<const uint8_t> operation,
				  std::span<const uint8_t, FP_MAC_LENGTH> mac)
{
	const auto challenge = consume_challenge();
	if (!challenge.has_value()) {
		return std::unexpected(challenge.error());
	}

	/* We expect the message to come from Fingerguard. */
	const auto computed_mac = compute_message_signature(
		session_key, context, as_operation_span("finger_guard"),
		operation, challenge->value);
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

std::expected<std::array<uint8_t, FP_MAC_LENGTH>, ec_error_list>
FpCryptoContext::sign_message(
	std::span<const uint8_t> context, std::span<const uint8_t> operation,
	std::span<const uint8_t, FP_CHALLENGE_SIZE> peer_challenge) const
{
	/*
	 * The Session Key is used to sign messages. Let's make sure
	 * it's available.
	 */
	if (!auth_enabled()) {
		return std::unexpected(EC_ERROR_ACCESS_DENIED);
	}

	return compute_message_signature(session_key, context,
					 as_operation_span("fpmcu"), operation,
					 peer_challenge);
}

static enum ec_status
fp_command_generate_nonce(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_generate_nonce *>(args->response);

	ScopedFastCpu fast_cpu;

	global_context.crypto.generate_nonce(r->nonce);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_GENERATE_NONCE, fp_command_generate_nonce,
		     EC_VER_MASK(0));

std::expected<void, ec_status>
fpsensor_context::establish_session(const ec_params_fp_establish_session &p)
{
	if (!crypto.session_nonce_set()) {
		CPRINTS("No existing session nonce");
		return std::unexpected(EC_RES_ACCESS_DENIED);
	}

	/* Do not establish session if any operation on templates is running */
	if (has_any_mode(FP_MODES_TEMPLATE_OPERATION)) {
		return std::unexpected(EC_RES_BUSY);
	}

	ScopedFastCpu fast_cpu;

	/* Avoid using old TPM Seed if the seed decryption fails. */
	crypto.clear_tpm_seed();

	/* Invalidate the existing context and templates to prevent
	 * adding new templates unauthorized by TA.
	 */
	reset_context();

	/* Reset SESSION_ESTABLISHED bit to avoid situation that the
	 * Fingerprint Auth is enabled with TPM Seed set using
	 * EC_CMD_FP_SEED_SET command (which is possible when the TPM Seed is
	 * not set, e.g. due to seed decryption error).
	 */
	crypto.set_auth_enabled(false);

	const auto new_session_key = crypto.derive_session_key(p.peer_nonce);
	if (!new_session_key.has_value()) {
		return std::unexpected(EC_RES_INVALID_PARAM);
	}
	crypto.session_key = *new_session_key;

	static_assert(sizeof(crypto.tpm_seed) == sizeof(p.enc_tpm_seed));
	CleanseWrapper<std::array<uint8_t, sizeof(crypto.tpm_seed)> > tpm_seed;

	if (!decrypt_data_with_session_key(crypto.session_key, p.enc_tpm_seed,
					   tpm_seed, p.nonce, p.tag,
					   as_operation_span("tpm_seed"))
		     .has_value()) {
		return std::unexpected(EC_RES_ERROR);
	}

	/* Set the TPM Seed. */
	crypto.set_tpm_seed(tpm_seed);
	crypto.clear_session_nonce();
	crypto.set_auth_enabled(true);

	return {};
}

static enum ec_status
fp_command_establish_session(struct host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_establish_session *>(
		args->params);

	return global_context.establish_session(*p).error_or(EC_RES_SUCCESS);
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
	if (!global_context.crypto.auth_enabled()) {
		return EC_RES_ACCESS_DENIED;
	}

	ScopedFastCpu fast_cpu;

	global_context.crypto.generate_challenge(r->challenge);

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
	return global_context.crypto.validate_request(context, operation, mac);
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
