/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "openssl/bn.h"
#include "openssl/mem.h"
#include "openssl/rand.h"

#include <array>
#include <expected>

namespace
{

std::expected<void, ec_error_list> decrypt_with_derived_key(
	const fp_auth_command_encryption_metadata &info,
	std::expected<FpEncryptionKey, ec_error_list> key_result,
	std::span<const uint8_t> enc_data, std::span<uint8_t> data)
{
	if (!key_result.has_value()) {
		CPRINTS("Failed to derive key");
		return std::unexpected(key_result.error());
	}

	return key_result->decrypt(data, enc_data, info.nonce, info.tag)
		.transform_error([](ec_error_list err) {
			CPRINTS("Failed to decipher data");
			return err;
		});
}

} /* namespace */

std::expected<fp_auth_command_encryption_metadata, ec_error_list>
FpAuthCommandCipher::create_metadata(uint16_t version)
{
	if (version != kSupportedVersion) {
		return std::unexpected(EC_ERROR_INVAL);
	}
	fp_auth_command_encryption_metadata info{};
	info.struct_version = version;
	RAND_bytes(info.nonce, sizeof(info.nonce));
	RAND_bytes(info.encryption_salt, sizeof(info.encryption_salt));
	return info;
}

bool FpAuthCommandCipher::is_valid_metadata(
	const fp_auth_command_encryption_metadata &info) noexcept
{
	return info.struct_version == kSupportedVersion;
}

std::expected<fp_auth_command_encryption_metadata, ec_error_list>
FpAuthCommandCipher::encrypt(
	uint16_t version,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	std::span<const uint8_t> data, std::span<uint8_t> enc_data)
{
	if (enc_data.size() != data.size()) {
		return std::unexpected(EC_ERROR_OVERFLOW);
	}

	auto info = create_metadata(version);
	if (!info.has_value()) {
		return std::unexpected(info.error());
	}

	const auto enc_key = FpEncryptionKey::derive(info->encryption_salt,
						     user_id, tpm_seed);
	if (!enc_key.has_value()) {
		return std::unexpected(enc_key.error());
	}

	return enc_key->encrypt(data, enc_data, info->nonce, info->tag)
		.transform([&] { return *info; });
}

std::expected<fp_auth_command_encryption_metadata, ec_error_list>
FpAuthCommandCipher::encrypt_in_place(
	uint16_t version,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	std::span<uint8_t> data)
{
	return encrypt(version, user_id, tpm_seed, data, data);
}

std::expected<void, ec_error_list> FpAuthCommandCipher::decrypt(
	const fp_auth_command_encryption_metadata &info,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	std::span<const uint8_t> enc_data, std::span<uint8_t> data)
{
	if (!is_valid_metadata(info)) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	if (enc_data.size() != data.size()) {
		CPRINTS("Data size mismatch");
		return std::unexpected(EC_ERROR_OVERFLOW);
	}

	return decrypt_with_derived_key(
		info,
		FpEncryptionKey::derive(info.encryption_salt, user_id,
					tpm_seed),
		enc_data, data);
}

FpPairingKey::FpPairingKey() noexcept = default;

FpPairingKey::FpPairingKey(const Bytes &bytes) noexcept : key_(bytes)
{
}

const FpPairingKey::Bytes &FpPairingKey::bytes() const noexcept
{
	return key_;
}

std::expected<FpPairingKey, ec_error_list>
FpPairingKey::from_ecdh(const FpP256Key &private_key,
			const fp_elliptic_curve_public_key &peer_pubkey)
{
	/*
	 * The Pairing Key is only used to produce the Session Key.
	 * It's not used as a key for symmetric encryption. It's okay
	 * to not apply KDF in this case.
	 */
	return FpP256Key::from_public_key(peer_pubkey)
		.and_then([&](const FpP256Key &public_key) {
			return private_key.compute_shared_secret(public_key);
		})
		.transform([](const auto &secret) {
			return FpPairingKey(secret);
		});
}

std::expected<ec_fp_encrypted_pairing_key, ec_error_list>
FpPairingKey::wrap(uint16_t version) const
{
	const auto info = FpAuthCommandCipher::create_metadata(version);
	if (!info.has_value()) {
		return std::unexpected(info.error());
	}

	const auto enc_key =
		FpEncryptionKey::derive_for_pairing_key(info->encryption_salt);
	if (!enc_key.has_value()) {
		return std::unexpected(enc_key.error());
	}

	ec_fp_encrypted_pairing_key encrypted{
		.info = *info,
		.data = {},
	};
	return enc_key
		->encrypt(key_, encrypted.data, encrypted.info.nonce,
			  encrypted.info.tag)
		.transform([&] { return encrypted; });
}

std::expected<FpPairingKey, ec_error_list>
FpPairingKey::unwrap(const fp_auth_command_encryption_metadata &info,
		     std::span<const uint8_t, FP_PAIRING_KEY_LEN> enc_data)
{
	if (!FpAuthCommandCipher::is_valid_metadata(info)) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	FpPairingKey key{};
	return decrypt_with_derived_key(info,
					FpEncryptionKey::derive_for_pairing_key(
						info.encryption_salt),
					enc_data, key.key_)
		.transform([&] { return key; });
}

std::expected<FpPairingKey, ec_error_list>
FpPairingKey::unwrap(const ec_fp_encrypted_pairing_key &encrypted)
{
	return unwrap(encrypted.info, encrypted.data);
}
