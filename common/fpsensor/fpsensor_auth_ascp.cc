/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_state.h"
#include "host_command.h"

#include <array>
#include <ascp/ascp.h>
#include <span>

std::expected<void, ec_status> FpCryptoContext::establish_ascp_pairing_key(
	std::span<const uint8_t, FP_ASCP_KEY_SIZE> pk_g)
{
	return create_pubkey_from_uncompressed_point(pk_g)
		.and_then(create_ec_key_from_pubkey)
		.transform_error(
			[](ec_error_list) { return EC_RES_INVALID_PARAM; })
		.and_then([this](const bssl::UniquePtr<EC_KEY> &public_key)
				  -> std::expected<void, ec_status> {
			CleanseWrapper<std::array<
				uint8_t, FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN> >
				sk_f{};
			if (ascp_get_sk_f(sk_f.data(), sk_f.size()) !=
			    EC_SUCCESS) {
				return std::unexpected(EC_RES_ERROR);
			}

			return create_ec_key_from_privkey(sk_f)
				.and_then([&public_key](
						  const bssl::UniquePtr<EC_KEY>
							  &priv) {
					return generate_ecdh_shared_secret_without_kdf(
						*priv, *public_key);
				})
				.transform([this](const auto &shared_secret) {
					pairing_key = shared_secret;
				})
				.transform_error([](ec_error_list) {
					return EC_RES_ERROR;
				});
		});
}

static ec_status fp_command_ascp_establish(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_ascp_establish *>(args->params);

	return global_context.crypto.establish_ascp_pairing_key(params->pk_g)
		.error_or(EC_RES_SUCCESS);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ASCP_ESTABLISH, fp_command_ascp_establish,
		     EC_VER_MASK(0));
