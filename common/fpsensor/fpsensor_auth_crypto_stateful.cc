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

constexpr uint16_t kSupportedMetadataVersion = 1;

[[nodiscard]] std::expected<fp_auth_command_encryption_metadata, ec_error_list>
create_encryption_metadata(uint16_t version)
{
	if (version != kSupportedMetadataVersion) {
		return std::unexpected(EC_ERROR_INVAL);
	}
	fp_auth_command_encryption_metadata info{};
	info.struct_version = version;
	RAND_bytes(info.nonce, sizeof(info.nonce));
	RAND_bytes(info.encryption_salt, sizeof(info.encryption_salt));
	return info;
}

[[nodiscard]] constexpr bool validate_encryption_metadata(
	const fp_auth_command_encryption_metadata &info) noexcept
{
	return info.struct_version == kSupportedMetadataVersion;
}

} /* namespace */

std::expected<fp_auth_command_encryption_metadata, ec_error_list>
encrypt_data(uint16_t version,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t> data, std::span<uint8_t> enc_data)
{
	if (enc_data.size() != data.size()) {
		return std::unexpected(EC_ERROR_OVERFLOW);
	}

	return create_encryption_metadata(version).and_then(
		[&](fp_auth_command_encryption_metadata info) {
			return derive_encryption_key(info.encryption_salt,
						     user_id, tpm_seed)
				.and_then([&](const FpEncryptionKey &enc_key) {
					return aes_128_gcm_encrypt(
						enc_key, data, enc_data,
						info.nonce, info.tag);
				})
				.transform([&]() { return info; });
		});
}

std::expected<ec_fp_encrypted_pairing_key, ec_error_list>
encrypt_pairing_key(uint16_t version,
		    std::span<const uint8_t, FP_PAIRING_KEY_LEN> data)
{
	return create_encryption_metadata(version).and_then(
		[&](const fp_auth_command_encryption_metadata &info) {
			return derive_pairing_key_encryption_key(
				       info.encryption_salt)
				.and_then([&](const FpEncryptionKey &enc_key) {
					ec_fp_encrypted_pairing_key encrypted{
						.info = info,
						.data = {},
					};
					return aes_128_gcm_encrypt(
						       enc_key, data,
						       encrypted.data,
						       encrypted.info.nonce,
						       encrypted.info.tag)
						.transform([&]() {
							return encrypted;
						});
				});
		});
}

static std::expected<void, ec_error_list> decrypt_with_derived_key(
	const struct fp_auth_command_encryption_metadata &info,
	std::expected<FpEncryptionKey, ec_error_list> key_result,
	std::span<const uint8_t> enc_data, std::span<uint8_t> data)
{
	return std::move(key_result)
		.or_else([](ec_error_list err) -> std::expected<FpEncryptionKey,
								ec_error_list> {
			CPRINTS("Failed to derive key");
			return std::unexpected(err);
		})
		.and_then([&](const FpEncryptionKey &enc_key) {
			return aes_128_gcm_decrypt(enc_key, data, enc_data,
						   info.nonce, info.tag)
				.or_else([](ec_error_list err)
						 -> std::expected<
							 void, ec_error_list> {
					CPRINTS("Failed to decipher data");
					return std::unexpected(err);
				});
		});
}

std::expected<void, ec_error_list>
decrypt_data(const struct fp_auth_command_encryption_metadata &info,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t> enc_data, std::span<uint8_t> data)
{
	if (!validate_encryption_metadata(info)) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	if (enc_data.size() != data.size()) {
		CPRINTS("Data size mismatch");
		return std::unexpected(EC_ERROR_OVERFLOW);
	}

	return decrypt_with_derived_key(
		info,
		derive_encryption_key(info.encryption_salt, user_id, tpm_seed),
		enc_data, data);
}

std::expected<CleanseWrapper<std::array<uint8_t, FP_PAIRING_KEY_LEN> >,
	      ec_error_list>
decrypt_pairing_key(const struct fp_auth_command_encryption_metadata &info,
		    std::span<const uint8_t, FP_PAIRING_KEY_LEN> enc_data)
{
	if (!validate_encryption_metadata(info)) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	CleanseWrapper<std::array<uint8_t, FP_PAIRING_KEY_LEN> > data{};
	return decrypt_with_derived_key(
		       info,
		       derive_pairing_key_encryption_key(info.encryption_salt),
		       enc_data, data)
		.transform([&]() { return data; });
}
