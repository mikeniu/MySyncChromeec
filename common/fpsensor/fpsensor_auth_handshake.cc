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
#include "scoped_fast_cpu.h"

#pragma GCC poison global_context

std::expected<fp_elliptic_curve_public_key, ec_status>
FpCryptoContext::establish_pairing_key_keygen()
{
	ScopedFastCpu fast_cpu;

	auto key = FpP256Key::generate();
	if (!key.has_value()) {
		ecdh_key.reset();
		return std::unexpected(EC_RES_UNAVAILABLE);
	}
	ecdh_key = *std::move(key);

	return ecdh_key->to_public_key().transform_error(
		[](ec_error_list) { return EC_RES_UNAVAILABLE; });
}

std::expected<ec_fp_encrypted_pairing_key, ec_status>
FpCryptoContext::establish_pairing_key_wrap(
	const fp_elliptic_curve_public_key &peers_pubkey)
{
	if (!ecdh_key.has_value()) {
		return std::unexpected(EC_RES_UNAVAILABLE);
	}

	ScopedFastCpu fast_cpu;

	const auto new_pairing_key =
		FpPairingKey::from_ecdh(*ecdh_key, peers_pubkey);
	if (!new_pairing_key.has_value()) {
		return std::unexpected(EC_RES_UNAVAILABLE);
	}

	const auto encrypted_key = new_pairing_key->wrap();
	if (!encrypted_key.has_value()) {
		return std::unexpected(EC_RES_UNAVAILABLE);
	}

	/* Deallocate the FPMCU's ECDH private key. */
	ecdh_key.reset();
	return *encrypted_key;
}

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
 * the TPM seed context via reset_context(). Because a compromised host/HAL
 * already possesses authority to assert Class 1/2 biometric results directly
 * to the host OS, re-establishing a local session domain does not grant
 * additional capabilities or alter the threat model boundary.
 */
std::expected<void, ec_status> FpCryptoContext::load_pairing_key(
	const ec_fp_encrypted_pairing_key &encrypted_pairing_key)
{
	ScopedFastCpu fast_cpu;

	return FpPairingKey::unwrap(encrypted_pairing_key)
		.transform([this](const FpPairingKey &decrypted_key) {
			pairing_key = decrypted_key;
		})
		.transform_error([](ec_error_list) {
			CPRINTS("load_pairing_key: Failed to decrypt pairing key");
			return EC_RES_UNAVAILABLE;
		});
}
