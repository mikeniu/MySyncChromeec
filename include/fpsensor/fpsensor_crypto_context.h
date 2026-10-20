/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint authenticated session nonce and challenge helpers */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CRYPTO_CONTEXT_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CRYPTO_CONTEXT_H_

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_utils.h"
#include "openssl/mem.h"
#include "openssl/rand.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <span>

struct FpSessionNonce {
	[[nodiscard]] static std::expected<void, ec_error_list>
	generate(std::span<uint8_t, FP_CK_SESSION_NONCE_LEN> nonce_state,
		 std::span<uint8_t, FP_CK_SESSION_NONCE_LEN> out,
		 uint32_t &encryption_status)
	{
		if (RAND_bytes(nonce_state.data(), nonce_state.size()) != 1) {
			return std::unexpected(EC_ERROR_HW_INTERNAL);
		}
		std::ranges::copy(nonce_state, out.begin());
		encryption_status |= FP_CONTEXT_SESSION_NONCE_SET;
		return {};
	}
};

struct FpAuthChallenge {
	static constexpr std::chrono::seconds kValidityWindow{ 5 };

	[[nodiscard]] static std::expected<void, ec_error_list>
	generate(std::span<uint8_t, FP_CHALLENGE_SIZE> challenge_state,
		 std::span<uint8_t, FP_CHALLENGE_SIZE> out, timestamp_t &ctime,
		 uint32_t &encryption_status)
	{
		if (RAND_bytes(challenge_state.data(),
			       challenge_state.size()) != 1) {
			return std::unexpected(EC_ERROR_HW_INTERNAL);
		}
		std::ranges::copy(challenge_state, out.begin());
		ctime = get_time();
		encryption_status |= FP_AUTH_CHALLENGE_SET;
		return {};
	}

	[[nodiscard]] static std::expected<void, ec_error_list>
	consume_and_verify(
		std::span<const uint8_t, SHA256_DIGEST_LENGTH> session_key,
		std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge,
		timestamp_t ctime, uint32_t &encryption_status,
		std::span<const uint8_t> context,
		std::span<const uint8_t> sender,
		std::span<const uint8_t> operation,
		std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac,
		FpTimestamp now = FpClock::now())
	{
		if (!(encryption_status & FP_AUTH_CHALLENGE_SET)) {
			return std::unexpected(EC_ERROR_ACCESS_DENIED);
		}
		encryption_status &= ~FP_AUTH_CHALLENGE_SET;

		const FpTimestamp issued{ FpClock::duration{ ctime.val } };
		if (now > issued + kValidityWindow) {
			return std::unexpected(EC_ERROR_TIMEOUT);
		}

		std::array<uint8_t, SHA256_DIGEST_LENGTH> computed_mac{};
		if (compute_message_signature(session_key, context, sender,
					      operation, challenge,
					      computed_mac) != EC_SUCCESS) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		if (CRYPTO_memcmp(mac.data(), computed_mac.data(),
				  mac.size()) != 0) {
			return std::unexpected(EC_ERROR_ACCESS_DENIED);
		}
		return {};
	}
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CRYPTO_CONTEXT_H_ */
