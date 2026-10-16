/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file fpsensor_crypto_mock.cc
 * @brief Mock fpsensor_crypto library
 */
#include "assert.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_utils.h"
#include "mock/fpsensor_crypto_mock.h"

#include <algorithm>
#include <expected>
#include <span>

extern "C" {
#include "sha256.h"
}

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

struct mock_ctrl_fpsensor_crypto mock_ctrl_fpsensor_crypto =
	MOCK_CTRL_DEFAULT_FPSENSOR_CRYPTO;

extern std::expected<void, ec_error_list>
hkdf_sha256_impl(std::span<uint8_t> out_key,
		 std::span<const std::span<const uint8_t> > ikms,
		 std::span<const uint8_t> salt, std::span<const uint8_t> info);

std::expected<void, ec_error_list>
hkdf_sha256(std::span<uint8_t> out_key,
	    std::span<const std::span<const uint8_t> > ikms,
	    std::span<const uint8_t> salt, std::span<const uint8_t> info)
{
	switch (mock_ctrl_fpsensor_crypto.output_type) {
	case MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_REAL:
		return hkdf_sha256_impl(out_key, ikms, salt, info);
	case MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_ZEROS:
		std::ranges::fill(out_key, 0);
		return {};
	case MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_FF:
		std::ranges::fill(out_key, 0xFF);
		return {};
	case MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_FAIL:
		return std::unexpected(EC_ERROR_UNKNOWN);
	default:
		assert(0);
		return std::unexpected(EC_ERROR_UNKNOWN);
	};
}
