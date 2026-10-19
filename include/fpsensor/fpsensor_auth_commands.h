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
[[nodiscard]] std::expected<void, ec_error_list>
validate_request(std::span<const uint8_t> context, std::string_view operation,
		 std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_AUTH_COMMANDS_H_ */
