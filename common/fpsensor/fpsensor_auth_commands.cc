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

#pragma GCC poison global_context

void FpCryptoContext::reset_session() noexcept
{
	session_nonce.reset();
	session_key.clear();
	tpm_seed.clear();
	set_auth_enabled(false);
}

FpSessionNonce
FpSessionNonce::generate(std::span<uint8_t, FP_CK_SESSION_NONCE_LEN> out)
{
	FpSessionNonce nonce{};
	RAND_bytes(nonce.value.data(), nonce.value.size());
	std::ranges::copy(nonce.value, out.begin());
	return nonce;
}

void FpCryptoContext::generate_nonce(
	std::span<uint8_t, FP_CK_SESSION_NONCE_LEN> out)
{
	ScopedFastCpu fast_cpu;

	session_nonce = FpSessionNonce::generate(out);
}

FpAuthChallenge
FpAuthChallenge::generate(std::span<uint8_t, FP_CHALLENGE_SIZE> out,
			  FpTimestamp now)
{
	FpAuthChallenge challenge{
		.value = {},
		.deadline = now + kValidityWindow,
	};
	RAND_bytes(challenge.value.data(), challenge.value.size());
	std::ranges::copy(challenge.value, out.begin());
	return challenge;
}

std::expected<FpAuthChallenge, ec_error_list>
FpAuthChallenge::consume(std::optional<FpAuthChallenge> &active,
			 FpTimestamp now) noexcept
{
	const auto challenge = std::exchange(active, std::nullopt);
	if (!challenge.has_value()) {
		return std::unexpected(EC_ERROR_ACCESS_DENIED);
	}
	if (challenge->is_expired(now)) {
		return std::unexpected(EC_ERROR_TIMEOUT);
	}
	return *challenge;
}

std::expected<void, ec_status>
FpCryptoContext::generate_challenge(std::span<uint8_t, FP_CHALLENGE_SIZE> out)
{
	/* The Session Key is used to sign messages. Let's make sure
	 * it's available. */
	if (!auth_enabled()) {
		return std::unexpected(EC_RES_ACCESS_DENIED);
	}

	ScopedFastCpu fast_cpu;

	active_challenge = FpAuthChallenge::generate(out);
	return {};
}

std::expected<FpAuthChallenge, ec_error_list>
FpCryptoContext::consume_challenge(FpTimestamp now) noexcept
{
	return FpAuthChallenge::consume(active_challenge, now);
}

std::expected<FpSessionKey, ec_error_list> FpCryptoContext::derive_session_key(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce) const
{
#ifdef CONFIG_PLATFORM_EC_FINGERPRINT_AUTH_METHOD_ASCP
	const auto context = as_operation_span("sdcp");
#else
	const std::span<const uint8_t> context{};
#endif
	return FpSessionKey::derive(session_nonce->bytes(), peer_nonce,
				    pairing_key.bytes(), context);
}

test_mockable std::expected<void, ec_error_list>
FpCryptoContext::validate_request(std::span<const uint8_t> context,
				  std::string_view operation,
				  std::span<const uint8_t, FP_MAC_LENGTH> mac)
{
	const auto challenge = consume_challenge();
	if (!challenge.has_value()) {
		return std::unexpected(challenge.error());
	}

	/* We expect the message to come from Fingerguard. */
	return session_key.verify(context, "finger_guard", operation,
				  challenge->value, mac);
}

std::expected<std::array<uint8_t, FP_MAC_LENGTH>, ec_error_list>
FpCryptoContext::sign_message(
	std::span<const uint8_t> context, std::string_view operation,
	std::span<const uint8_t, FP_CHALLENGE_SIZE> peer_challenge) const
{
	/*
	 * The Session Key is used to sign messages. Let's make sure
	 * it's available.
	 */
	if (!auth_enabled()) {
		return std::unexpected(EC_ERROR_ACCESS_DENIED);
	}

	return session_key.sign(context, "fpmcu", operation, peer_challenge);
}

std::expected<void, ec_status>
FpSensorContext::establish_session(const ec_params_fp_establish_session &p)
{
	if (!crypto.session_nonce.has_value()) {
		CPRINTS("No existing session nonce");
		return std::unexpected(EC_RES_ACCESS_DENIED);
	}

	/* Do not establish session if any operation on templates is running */
	if (has_any_mode(FP_MODES_TEMPLATE_OPERATION)) {
		return std::unexpected(EC_RES_BUSY);
	}

	ScopedFastCpu fast_cpu;

	/* Avoid using old TPM Seed if the seed decryption fails. */
	crypto.tpm_seed.clear();

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

	static_assert(sizeof(FpTpmSeed::Bytes) == sizeof(p.enc_tpm_seed));
	CleanseWrapper<FpTpmSeed::Bytes> tpm_seed;
	if (!crypto.session_key
		     .decrypt(p.enc_tpm_seed, tpm_seed, p.nonce, p.tag,
			      "tpm_seed")
		     .has_value()) {
		return std::unexpected(EC_RES_ERROR);
	}

	/* Set the TPM Seed. */
	crypto.tpm_seed.set(tpm_seed);
	crypto.session_nonce.reset();
	crypto.set_auth_enabled(true);
	return {};
}
