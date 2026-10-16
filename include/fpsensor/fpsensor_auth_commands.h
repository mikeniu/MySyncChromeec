/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_AUTH_COMMANDS_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_AUTH_COMMANDS_H_

#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "fpsensor/fpsensor_state.h"
#include "openssl/sha.h"

#include <array>
#include <expected>
#include <span>
#include <string_view>

/**
 * View a string literal / string_view operation name as a byte span for MAC
 * signing and verification.
 */
[[nodiscard]] inline std::span<const uint8_t>
as_operation_span(std::string_view operation) noexcept
{
	return { reinterpret_cast<const uint8_t *>(operation.data()),
		 operation.size() };
}

/**
 * Validate request from Trusted Application.
 *
 * @param[in] context the context.
 * @param[in] operation the requested operation.
 * @param[in] mac the mac that authenticates the request.
 * @return empty expected if the request was validated successfully.
 * @return EC_ERROR_ACCESS_DENIED if failed to validate the request.
 * @return EC_ERROR_TIMEOUT if the challenge is too old.
 * @return EC_ERROR_INVAL if failed to compute expected mac.
 */
std::expected<void, ec_error_list>
validate_request(std::span<const uint8_t> context,
		 std::span<const uint8_t> operation,
		 std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac);

/**
 * Call generate_session_key with current pairing key and context.
 *
 * @param[in] fpmcu_nonce the session nonce on FPMCU side
 * @param[in] peer_nonce the session nonce on peer side
 * @param[in] pairing_key the pairing key to derive the session key from
 *
 * @return the output session key on success
 * @return EC_ERROR_* on error
 */
std::expected<CleanseWrapper<std::array<uint8_t, SHA256_DIGEST_LENGTH> >,
	      ec_error_list>
generate_session_key_with_context(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce,
	std::span<const uint8_t, FP_PAIRING_KEY_LEN> pairing_key =
		global_context.crypto.pairing_key);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_AUTH_COMMANDS_H_ */
