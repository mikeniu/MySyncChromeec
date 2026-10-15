/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TODO(b/286119221): refactor FPMCU code so that functions in this file don't
 * rely on global state. */

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

namespace
{

constexpr uint16_t kSupportedMetadataVersion = 1;

[[nodiscard]] ec_error_list
init_encryption_metadata(uint16_t version,
			 fp_auth_command_encryption_metadata &info)
{
	if (version != kSupportedMetadataVersion) {
		return EC_ERROR_INVAL;
	}
	info.struct_version = version;
	RAND_bytes(info.nonce, sizeof(info.nonce));
	RAND_bytes(info.encryption_salt, sizeof(info.encryption_salt));
	return EC_SUCCESS;
}

[[nodiscard]] constexpr bool validate_encryption_metadata(
	const fp_auth_command_encryption_metadata &info) noexcept
{
	return info.struct_version == kSupportedMetadataVersion;
}

} /* namespace */

enum ec_error_list
encrypt_data(uint16_t version, struct fp_auth_command_encryption_metadata &info,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t> data, std::span<uint8_t> enc_data)
{
	if (enc_data.size() != data.size()) {
		return EC_ERROR_OVERFLOW;
	}

	enum ec_error_list ret = init_encryption_metadata(version, info);
	if (ret != EC_SUCCESS) {
		return ret;
	}

	FpEncryptionKey enc_key;
	ret = derive_encryption_key(enc_key, info.encryption_salt, user_id,
				    tpm_seed);
	if (ret != EC_SUCCESS) {
		return ret;
	}

	return aes_128_gcm_encrypt(enc_key, data, enc_data, info.nonce,
				   info.tag);
}

enum ec_error_list
encrypt_pairing_key(uint16_t version,
		    struct fp_auth_command_encryption_metadata &info,
		    std::span<const uint8_t, FP_PAIRING_KEY_LEN> data,
		    std::span<uint8_t, FP_PAIRING_KEY_LEN> enc_data)
{
	enum ec_error_list ret = init_encryption_metadata(version, info);
	if (ret != EC_SUCCESS) {
		return ret;
	}

	FpEncryptionKey enc_key;
	ret = derive_pairing_key_encryption_key(enc_key, info.encryption_salt);
	if (ret != EC_SUCCESS) {
		return ret;
	}

	return aes_128_gcm_encrypt(enc_key, data, enc_data, info.nonce,
				   info.tag);
}

enum ec_error_list
decrypt_data(const struct fp_auth_command_encryption_metadata &info,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t> enc_data, std::span<uint8_t> data)
{
	if (!validate_encryption_metadata(info)) {
		return EC_ERROR_INVAL;
	}

	FpEncryptionKey enc_key;
	enum ec_error_list ret = derive_encryption_key(
		enc_key, info.encryption_salt, user_id, tpm_seed);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to derive key");
		return ret;
	}

	if (enc_data.size() != data.size()) {
		CPRINTS("Data size mismatch");
		return EC_ERROR_OVERFLOW;
	}

	ret = aes_128_gcm_decrypt(enc_key, data, enc_data, info.nonce,
				  info.tag);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to decipher data");
		return ret;
	}

	return EC_SUCCESS;
}

enum ec_error_list
decrypt_pairing_key(const struct fp_auth_command_encryption_metadata &info,
		    std::span<const uint8_t, FP_PAIRING_KEY_LEN> enc_data,
		    std::span<uint8_t, FP_PAIRING_KEY_LEN> data)
{
	if (!validate_encryption_metadata(info)) {
		return EC_ERROR_INVAL;
	}

	FpEncryptionKey enc_key;
	enum ec_error_list ret = derive_pairing_key_encryption_key(
		enc_key, info.encryption_salt);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to derive key");
		return ret;
	}

	ret = aes_128_gcm_decrypt(enc_key, data, enc_data, info.nonce,
				  info.tag);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to decipher data");
		return ret;
	}

	return EC_SUCCESS;
}
