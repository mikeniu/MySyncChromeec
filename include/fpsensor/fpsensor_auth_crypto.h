/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_AUTH_CRYPTO_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_AUTH_CRYPTO_H_

#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "openssl/ec.h"
#include "openssl/sha.h"

#include <array>
#include <expected>
#include <span>

/**
 * Create a @fp_elliptic_curve_public_key from a 65-byte SEC1 uncompressed
 * elliptic curve point (0x04 || x || y).
 *
 * @param[in] point 65-byte uncompressed point buffer
 *
 * @return @p fp_elliptic_curve_public_key on success
 * @return EC_ERROR_INVAL if the leading octet is not 0x04
 */
[[nodiscard]] std::expected<fp_elliptic_curve_public_key, ec_error_list>
create_pubkey_from_uncompressed_point(
	std::span<const uint8_t, FP_ASCP_KEY_SIZE> point) noexcept;

/**
 * Create a @fp_elliptic_curve_public_key with the content of boringssl @p
 * EC_KEY.
 *
 * @param[in] key boringssl key
 *
 * @return @p fp_elliptic_curve_public_key on success
 * @return EC_ERROR_INVAL on error
 */
[[nodiscard]] std::expected<fp_elliptic_curve_public_key, ec_error_list>
create_pubkey_from_ec_key(const EC_KEY &key);

/**
 * Create a boringssl @EC_KEY from the @p fp_elliptic_curve_public_key content.
 *
 * @param[in] pubkey public key structure
 *
 * @return @p EC_KEY on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] std::expected<bssl::UniquePtr<EC_KEY>, ec_error_list>
create_ec_key_from_pubkey(const fp_elliptic_curve_public_key &pubkey);

/**
 * Create a boringssl @EC_KEY from a private key.
 *
 * @param[in] privkey private key
 *
 * @return @p EC_KEY on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] std::expected<bssl::UniquePtr<EC_KEY>, ec_error_list>
create_ec_key_from_privkey(
	std::span<const uint8_t, FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN> privkey);

/**
 * Encrypt the data with a specific version of encryption method and output
 * the metadata and encrypted data.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the TPM
 * seed, rollback secret and user_id.
 *
 * @param[in] version the version of the encryption method
 * @param[in] user_id the user_id used for deriving secret
 * @param[in] tpm_seed the seed from the TPM for deriving secret
 * @param[in] data the data that need to be encrypted
 * @param[out] enc_data the encrypted data
 *
 * @return encryption metadata on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] std::expected<fp_auth_command_encryption_metadata, ec_error_list>
encrypt_data(uint16_t version,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t> data, std::span<uint8_t> enc_data);

/**
 * Encrypt the data in place with a specific version of encryption method and
 * output the metadata and encrypted data.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the TPM
 * seed, rollback secret and user_id.
 *
 * @param[in] version the version of the encryption method
 * @param[in] user_id the user_id used for deriving secret
 * @param[in] tpm_seed the seed from the TPM for deriving secret
 * @param[in,out] data the data that need to be encrypted in place
 *
 * @return encryption metadata on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] inline std::expected<fp_auth_command_encryption_metadata,
				   ec_error_list>
encrypt_data_in_place(uint16_t version,
		      std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		      std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
		      std::span<uint8_t> data)
{
	return encrypt_data(version, user_id, tpm_seed, data, data);
}

/**
 * Encrypt the Pairing Key with a specific version of encryption method and
 * output the metadata and encrypted data.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the rollback
 * secret and optional OTP key.
 *
 * Please note that this function SHOULD NOT be used for anything other than
 * Pairing Key.
 *
 * @param[in] version the version of the encryption method
 * @param[in] data the data that need to be encrypted
 *
 * @return encrypted pairing key (metadata + ciphertext) on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] std::expected<ec_fp_encrypted_pairing_key, ec_error_list>
encrypt_pairing_key(uint16_t version,
		    std::span<const uint8_t, FP_PAIRING_KEY_LEN> data);

/**
 * Decrypt the encrypted data.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the TPM
 * seed, rollback secret and user_id.
 *
 * @param[in] info the metadata of the encryption output
 * @param[in] user_id the user_id used for deriving secret
 * @param[in] tpm_seed the seed from the TPM for deriving secret
 * @param[in] enc_data the encrypted data
 * @param[out] data the decrypted data
 *
 * @return empty expected on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] std::expected<void, ec_error_list>
decrypt_data(const struct fp_auth_command_encryption_metadata &info,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t> enc_data, std::span<uint8_t> data);

/**
 * Decrypt the encrypted pairing key.
 *
 * version 1 is 128 bit AES-GCM, and the encryption key is bound to the rollback
 * secret and optional OTP key.
 *
 * Please note that this function SHOULD NOT be used for anything other than
 * Pairing Key.
 *
 * @param[in] info the metadata of the encryption output
 * @param[in] enc_data the encrypted data
 *
 * @return decrypted pairing key on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] std::expected<
	CleanseWrapper<std::array<uint8_t, FP_PAIRING_KEY_LEN> >, ec_error_list>
decrypt_pairing_key(const struct fp_auth_command_encryption_metadata &info,
		    std::span<const uint8_t, FP_PAIRING_KEY_LEN> enc_data);

/**
 * Generate the ECDH shared secret from private key and public key without
 * applying any KDF function on the result.
 *
 * IMPORTANT NOTE:
 * The result is not uniformly distributed, so it should not be used for
 * anything which requires that property, e.g. symmetric ciphers. The result
 * should be used as an input to a KDF to produce symmetric key.
 *
 * @param[in] private_key the private key of the ECDH
 * @param[in] public_key the public key of the ECDH
 *
 * @return the shared secret on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] std::expected<
	CleanseWrapper<std::array<uint8_t, FP_PAIRING_KEY_LEN> >, ec_error_list>
generate_ecdh_shared_secret_without_kdf(const EC_KEY &private_key,
					const EC_KEY &public_key);

/**
 * Generate a session key that is derived from FPMCU nonce, peer nonce and
 * the Pairing Key.
 *
 * @param[in] fpmcu_nonce the session nonce on FPMCU side
 * @param[in] peer_nonce the session nonce on peer side
 * @param[in] pairing_key the Pairing Key
 * @param[in] session_context the session context
 *
 * @return the output session key on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] std::expected<
	CleanseWrapper<std::array<uint8_t, SHA256_DIGEST_LENGTH> >,
	ec_error_list>
generate_session_key(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce,
	std::span<const uint8_t, FP_PAIRING_KEY_LEN> pairing_key,
	std::span<const uint8_t> session_context);

/**
 * Decrypt the data with a session key.
 *
 * @param[in] session_key the session key
 * @param[in] input the encrypted data
 * @param[out] output the decrypted data
 * @param[in] nonce the AES256-GCM nonce
 * @param[in] tag the AES256-GCM tag
 * @param[in] aad the AES256-GCM additional auth data
 *
 * @return empty expected on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] std::expected<void, ec_error_list> decrypt_data_with_session_key(
	std::span<const uint8_t, SHA256_DIGEST_LENGTH> session_key,
	std::span<const uint8_t> input, std::span<uint8_t> output,
	std::span<const uint8_t, FP_AES_KEY_NONCE_BYTES> nonce,
	std::span<const uint8_t, FP_AES_KEY_TAG_BYTES> tag,
	std::span<const uint8_t> aad);

/**
 * Compute session message signature
 *
 * @param[in] session_key the session key
 * @param[in] context the context e.g. user id
 * @param[in] sender the sender name, e.g. "fpmcu"
 * @param[in] operation the authenticated operation
 * @param[in] challenge the challenge
 *
 * @return the message signature on success
 * @return EC_ERROR_* on error
 */
[[nodiscard]] std::expected<std::array<uint8_t, SHA256_DIGEST_LENGTH>,
			    ec_error_list>
compute_message_signature(
	std::span<const uint8_t, SHA256_DIGEST_LENGTH> session_key,
	std::span<const uint8_t> context, std::span<const uint8_t> sender,
	std::span<const uint8_t> operation,
	std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_AUTH_CRYPTO_H_ */
