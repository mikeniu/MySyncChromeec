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
static std::optional<FpP256Key> ecdh_key;

/* The FPMCU pairing key. */
static std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;

enum ec_error_list check_context_cleared()
{
	for (uint8_t partial : global_context.user_id)
		if (partial != 0)
			return EC_ERROR_ACCESS_DENIED;
	if (global_context.templ_valid != 0)
		return EC_ERROR_ACCESS_DENIED;
	if (global_context.templ_dirty != 0)
		return EC_ERROR_ACCESS_DENIED;
	if (global_context.positive_match_secret_state.template_matched !=
	    FP_NO_SUCH_TEMPLATE)
		return EC_ERROR_ACCESS_DENIED;
	if (global_context.fp_encryption_status & FP_CONTEXT_USER_ID_SET)
		return EC_ERROR_ACCESS_DENIED;
	return EC_SUCCESS;
}

static enum ec_host_cmd_status
fp_command_establish_pairing_key_keygen(struct ec_host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_establish_pairing_key_keygen *>(
		args->output_buf);

	ScopedFastCpu fast_cpu;

	auto key = FpP256Key::generate();
	if (!key.has_value()) {
		ecdh_key.reset();
		return EC_HOST_CMD_UNAVAILABLE;
	}
	ecdh_key = *std::move(key);

	const auto pubkey = ecdh_key->to_public_key();
	if (!pubkey.has_value()) {
		return EC_HOST_CMD_UNAVAILABLE;
	}

	r->pubkey = *pubkey;

	args->output_buf_size = sizeof(*r);
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_RESP_ONLY(
	EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN,
	fp_command_establish_pairing_key_keygen, EC_VER_MASK(0),
	struct ec_response_fp_establish_pairing_key_keygen);

static enum ec_host_cmd_status
fp_command_establish_pairing_key_wrap(struct ec_host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_establish_pairing_key_wrap *>(
			args->input_buf);
	auto *r = static_cast<ec_response_fp_establish_pairing_key_wrap *>(
		args->output_buf);

	if (!ecdh_key.has_value()) {
		return EC_HOST_CMD_UNAVAILABLE;
	}

	ScopedFastCpu fast_cpu;

	const auto wrapped = FpPairingKey::establish_and_wrap(
		*ecdh_key, params->peers_pubkey);
	if (!wrapped.has_value()) {
		return EC_HOST_CMD_UNAVAILABLE;
	}
	r->encrypted_pairing_key = *wrapped;

	/* Deallocate the FPMCU's ECDH private key. */
	ecdh_key.reset();

	args->output_buf_size = sizeof(*r);
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP,
		    fp_command_establish_pairing_key_wrap, EC_VER_MASK(0),
		    struct ec_params_fp_establish_pairing_key_wrap,
		    struct ec_response_fp_establish_pairing_key_wrap);

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
static enum ec_host_cmd_status
fp_command_load_pairing_key(struct ec_host_cmd_handler_args *args)
{
	const auto *params = static_cast<const ec_params_fp_load_pairing_key *>(
		args->input_buf);

	ScopedFastCpu fast_cpu;

	if (!FpPairingKey::unwrap_into(params->encrypted_pairing_key,
				       pairing_key)
		     .has_value()) {
		CPRINTS("load_pairing_key: Failed to decrypt pairing key");
		return EC_HOST_CMD_UNAVAILABLE;
	}

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_FP_LOAD_PAIRING_KEY,
			     fp_command_load_pairing_key, EC_VER_MASK(0),
			     ec_params_fp_load_pairing_key);

enum ec_error_list generate_session_key_with_context(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce,
	std::span<uint8_t, SHA256_DIGEST_LENGTH> session_key)
{
	return generate_session_key(fpmcu_nonce, peer_nonce, pairing_key, {},
				    session_key);
}
