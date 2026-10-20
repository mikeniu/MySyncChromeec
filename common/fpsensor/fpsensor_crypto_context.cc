/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto_context.h"
#include "scoped_fast_cpu.h"

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#pragma GCC poison global_context

#if defined(CONFIG_CROS_EC_RO)
#error "fpsensor code should not be in RO image."
#endif

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

void FpCryptoContext::reset_user_context() noexcept
{
	encrypted_template_ready = false;
	active_challenge.reset();
	user_id.clear();
}

void FpCryptoContext::reset_session() noexcept
{
	session_nonce.reset();
	session_key.clear();
	tpm_seed.clear();
	set_auth_enabled(false);
}

void FpCryptoContext::generate_nonce(
	std::span<uint8_t, FP_CK_SESSION_NONCE_LEN> out)
{
	ScopedFastCpu fast_cpu;

	session_nonce = FpSessionNonce::generate(out);
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
