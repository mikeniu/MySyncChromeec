/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ascp/ascp.h"
#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_state.h"

#include <array>
#include <span>

#pragma GCC poison global_context

std::expected<void, ec_status> FpCryptoContext::establish_ascp_pairing_key(
	std::span<const uint8_t, FP_ASCP_KEY_SIZE> pk_g)
{
	const auto public_key = FpP256Key::from_uncompressed_point(pk_g);
	if (!public_key.has_value()) {
		return std::unexpected(EC_RES_INVALID_PARAM);
	}

	CleanseWrapper<std::array<uint8_t, FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN> >
		sk_f{};
	if (ascp_get_sk_f(sk_f.data(), sk_f.size()) != EC_SUCCESS) {
		return std::unexpected(EC_RES_ERROR);
	}

	const auto private_key = FpP256Key::from_private_key(sk_f);
	if (!private_key.has_value()) {
		return std::unexpected(EC_RES_ERROR);
	}

	const auto shared_secret =
		private_key->compute_shared_secret(*public_key);
	if (!shared_secret.has_value()) {
		return std::unexpected(EC_RES_ERROR);
	}

	pairing_key = FpPairingKey(*shared_secret);
	return {};
}
