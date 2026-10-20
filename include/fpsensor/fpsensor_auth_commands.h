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

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_AUTH_COMMANDS_H_ */
