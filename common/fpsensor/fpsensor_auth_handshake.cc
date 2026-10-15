/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_state.h"
#include "host_command.h"
#include "scoped_fast_cpu.h"

/* Pointer to the FPMCU's ECDH private key */
static bssl::UniquePtr<EC_KEY> ecdh_key;

/* The FPMCU pairing key. */
static std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;

std::expected<void, ec_error_list>
check_context_cleared(const fpsensor_context &ctx)
{
	if (!ctx.is_cleared()) {
		return std::unexpected(EC_ERROR_ACCESS_DENIED);
	}
	return {};
}

static enum ec_status
fp_command_establish_pairing_key_keygen(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_establish_pairing_key_keygen *>(
		args->response);

	ScopedFastCpu fast_cpu;

	ecdh_key = generate_elliptic_curve_key();
	if (ecdh_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	const auto pubkey = create_pubkey_from_ec_key(*ecdh_key);
	if (!pubkey.has_value()) {
		return EC_RES_UNAVAILABLE;
	}

	r->pubkey = *pubkey;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN,
		     fp_command_establish_pairing_key_keygen, EC_VER_MASK(0));

static enum ec_status
fp_command_establish_pairing_key_wrap(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_establish_pairing_key_wrap *>(
			args->params);
	auto *r = static_cast<ec_response_fp_establish_pairing_key_wrap *>(
		args->response);

	if (ecdh_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	ScopedFastCpu fast_cpu;

	bssl::UniquePtr<EC_KEY> public_key =
		create_ec_key_from_pubkey(params->peers_pubkey);
	if (public_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	/*
	 * The Pairing Key is only used to produce the Session Key.
	 * It's not used as a key for symmetric encryption. It's okay
	 * to not apply KDF in this case.
	 */
	const auto new_pairing_key =
		generate_ecdh_shared_secret_without_kdf(*ecdh_key, *public_key);
	if (!new_pairing_key.has_value()) {
		return EC_RES_UNAVAILABLE;
	}

	const auto encrypted_pairing_key = encrypt_pairing_key(
		FP_AES_KEY_ENC_METADATA_VERSION, *new_pairing_key);
	if (!encrypted_pairing_key.has_value()) {
		return EC_RES_UNAVAILABLE;
	}
	r->encrypted_pairing_key = *encrypted_pairing_key;

	/* Deallocate the FPMCU's ECDH private key. */
	ecdh_key = nullptr;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP,
		     fp_command_establish_pairing_key_wrap, EC_VER_MASK(0));

/*
 * DESIGN NOTE:
 *
 * Reloading the pairing key is intentionally permitted without preconditions
 * to allow re-establishing a session (e.g., after a HAL restart).
 *
 * The primary purpose of the pairing key is to ensure the Trusted Application
 * (TA) talks to the correct FPMCU via a shared session key derived during
 * session establishment. Changing or reloading the pairing key while a session
 * is active does not invalidate the existing session key or affect loaded
 * templates.
 *
 * Isolation and security rely entirely on the session establishment flow:
 * establishing a new session mandatorily clears existing templates and resets
 * the TPM seed context via fp_reset_context(). Because a compromised host/HAL
 * already possesses authority to assert Class 1/2 biometric results directly
 * to the host OS, re-establishing a local session domain does not grant
 * additional capabilities or alter the threat model boundary.
 */
static enum ec_status
fp_command_load_pairing_key(struct host_cmd_handler_args *args)
{
	const auto *params = static_cast<const ec_params_fp_load_pairing_key *>(
		args->params);

	ScopedFastCpu fast_cpu;

	const auto decrypted_key =
		decrypt_pairing_key(params->encrypted_pairing_key.info,
				    params->encrypted_pairing_key.data);
	if (!decrypted_key.has_value()) {
		CPRINTS("load_pairing_key: Failed to decrypt pairing key");
		return EC_RES_UNAVAILABLE;
	}
	pairing_key = *decrypted_key;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_LOAD_PAIRING_KEY, fp_command_load_pairing_key,
		     EC_VER_MASK(0));

std::expected<CleanseWrapper<std::array<uint8_t, SHA256_DIGEST_LENGTH> >,
	      ec_error_list>
generate_session_key_with_context(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce)
{
	return generate_session_key(fpmcu_nonce, peer_nonce, pairing_key, {});
}
