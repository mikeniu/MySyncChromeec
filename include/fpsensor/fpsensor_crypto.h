/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor crypto operations */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CRYPTO_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CRYPTO_H_

#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "openssl/sha.h"

#include <array>
#include <cstdint>
#include <expected>
#include <span>

/**
 * 128-bit hardware-bound AES-GCM encryption key. Automatically cleansed on
 * destruction.
 */
class FpEncryptionKey {
    public:
	using Bytes = std::array<uint8_t, 16>;

	FpEncryptionKey() noexcept;
	FpEncryptionKey(const Bytes &bytes) noexcept;

	/**
	 * Derive hardware encryption key from rollback secret, |salt|, |info|,
	 * and |tpm_seed|.
	 */
	[[nodiscard]] static std::expected<FpEncryptionKey, ec_error_list>
	derive(std::span<const uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES> salt,
	       std::span<const uint8_t> info,
	       std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);

	/**
	 * Derive Pairing Key encryption key from rollback secret, optional OTP
	 * key, and |salt|.
	 */
	[[nodiscard]] static std::expected<FpEncryptionKey, ec_error_list>
	derive_for_pairing_key(
		std::span<const uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES> salt);

	/**
	 * Encrypt |plaintext| into |ciphertext| using AES-128-GCM with this
	 * key.
	 */
	[[nodiscard]] std::expected<void, ec_error_list>
	encrypt(std::span<const uint8_t> plaintext,
		std::span<uint8_t> ciphertext, std::span<const uint8_t> nonce,
		std::span<uint8_t> tag) const;

	/**
	 * Decrypt |ciphertext| into |plaintext| using AES-128-GCM with this
	 * key.
	 */
	[[nodiscard]] std::expected<void, ec_error_list>
	decrypt(std::span<uint8_t> plaintext,
		std::span<const uint8_t> ciphertext,
		std::span<const uint8_t> nonce,
		std::span<const uint8_t> tag) const;

	[[nodiscard]] const Bytes &bytes() const noexcept;

	[[nodiscard]] uint8_t *data() noexcept
	{
		return key_.data();
	}

	[[nodiscard]] const uint8_t *data() const noexcept
	{
		return key_.data();
	}

	[[nodiscard]] constexpr size_t size() const noexcept
	{
		return sizeof(Bytes);
	}

	[[nodiscard]] uint8_t &operator[](size_t idx) noexcept
	{
		return key_[idx];
	}

	[[nodiscard]] const uint8_t &operator[](size_t idx) const noexcept
	{
		return key_[idx];
	}

	[[nodiscard]] operator std::span<uint8_t>() noexcept
	{
		return key_;
	}

	[[nodiscard]] operator std::span<const uint8_t>() const noexcept
	{
		return key_;
	}

    private:
	CleanseWrapper<Bytes> key_;
};
static_assert(sizeof(FpEncryptionKey) == 16,
	      "Encryption key must be 128 bits.");
static_assert(sizeof(FpEncryptionKey) <= CONFIG_ROLLBACK_SECRET_SIZE);

/**
 * 32-byte positive match secret derived from hardware key material, user_id,
 * and a per-template salt. Automatically cleansed on destruction.
 */
class FpPositiveMatchSecret {
    public:
	using Bytes = std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES>;

	FpPositiveMatchSecret() noexcept;
	explicit FpPositiveMatchSecret(const Bytes &bytes) noexcept;

	/**
	 * Derive positive match secret from |input_positive_match_salt|,
	 * |user_id|, and |tpm_seed|.
	 */
	[[nodiscard]] static std::expected<FpPositiveMatchSecret, ec_error_list>
	derive(std::span<const uint8_t, FP_POSITIVE_MATCH_SALT_BYTES>
		       input_positive_match_salt,
	       std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	       std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);

	[[nodiscard]] const Bytes &bytes() const noexcept;

	[[nodiscard]] const uint8_t *data() const noexcept
	{
		return secret_.data();
	}

	[[nodiscard]] constexpr size_t size() const noexcept
	{
		return sizeof(Bytes);
	}

    private:
	CleanseWrapper<Bytes> secret_;
};

/**
 * Computes HMAC-SHA256
 *
 * Calculate HMAC-SHA256 from data that are not continous.
 *
 * @param[in] key the key to use in HMAC
 * @param[in] inputs the span of inputs that will be hashed
 * @return HMAC digest on success and error code otherwise.
 */
[[nodiscard]] std::expected<std::array<uint8_t, SHA256_DIGEST_LENGTH>,
			    ec_error_list>
hmac_sha256(std::span<const uint8_t> key,
	    std::span<const std::span<const uint8_t> > inputs);

[[nodiscard]] inline enum ec_error_list
hmac_sha256(std::span<const uint8_t> key,
	    std::span<const std::span<const uint8_t> > inputs,
	    std::span<uint8_t, SHA256_DIGEST_LENGTH> output)
{
	const auto res = hmac_sha256(key, inputs);
	if (!res.has_value()) {
		return res.error();
	}
	std::copy(res->begin(), res->end(), output.begin());
	return EC_SUCCESS;
}

/**
 * Computes HKDF (as specified by RFC 5869) using SHA-256 as the digest.
 *
 * @param[out] out_key buffer to hold output key material. Max size must be less
 * than or equal to 255 * 32 (SHA256_DIGEST_SIZE) bytes = 8160 bytes.
 * @param[in] ikms input keying material.
 * @param[in] salt optional salt value (a non-secret random value).
 * @param[in] info optional context and application specific information (can be
 * a zero-length string).
 * @return true on success
 * @return false on failure
 */
[[nodiscard]] bool hkdf_sha256(std::span<uint8_t> out_key,
			       std::span<const std::span<const uint8_t> > ikms,
			       std::span<const uint8_t> salt,
			       std::span<const uint8_t> info);

/**
 * Encrypt |plaintext| using AES-GCM128.
 *
 * @param key the key to use in AES.
 * @param plaintext the plain text to encrypt.
 * @param ciphertext buffer to hold encryption result.
 * @param nonce the nonce value to use in GCM128.
 * @param tag the tag to hold the authenticator after encryption.
 * @return EC_SUCCESS on success and error code otherwise.
 */
[[nodiscard]] enum ec_error_list
aes_128_gcm_encrypt(std::span<const uint8_t> key,
		    std::span<const uint8_t> plaintext,
		    std::span<uint8_t> ciphertext,
		    std::span<const uint8_t> nonce, std::span<uint8_t> tag);

/**
 * Decrypt |plaintext| using AES-GCM128.
 *
 * @param key the key to use in AES.
 * @param ciphertext the cipher text to decrypt.
 * @param plaintext buffer to hold decryption result.
 * @param nonce the nonce value to use in GCM128.
 * @param tag the tag to compare against when decryption finishes.
 * @return EC_SUCCESS on success and error code otherwise.
 */
[[nodiscard]] enum ec_error_list
aes_128_gcm_decrypt(std::span<const uint8_t> key, std::span<uint8_t> plaintext,
		    std::span<const uint8_t> ciphertext,
		    std::span<const uint8_t> nonce,
		    std::span<const uint8_t> tag);

#ifndef CONFIG_ZEPHYR
[[nodiscard]] inline enum ec_error_list
derive_encryption_key(std::span<uint8_t> out_key, std::span<const uint8_t> salt,
		      std::span<const uint8_t> info,
		      std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	if (out_key.size() != sizeof(FpEncryptionKey::Bytes) ||
	    salt.size() != FP_CONTEXT_ENCRYPTION_SALT_BYTES) {
		return EC_ERROR_INVAL;
	}
	const auto key = FpEncryptionKey::derive(
		std::span<const uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES>(
			salt.data(), FP_CONTEXT_ENCRYPTION_SALT_BYTES),
		info, tpm_seed);
	if (!key.has_value()) {
		return key.error();
	}
	std::copy(key->bytes().begin(), key->bytes().end(), out_key.begin());
	return EC_SUCCESS;
}

[[nodiscard]] inline enum ec_error_list derive_positive_match_secret(
	std::span<uint8_t> output,
	std::span<const uint8_t> input_positive_match_salt,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	if (output.size() < FP_POSITIVE_MATCH_SECRET_BYTES ||
	    input_positive_match_salt.size() < FP_POSITIVE_MATCH_SALT_BYTES) {
		return EC_ERROR_INVAL;
	}
	const auto secret = FpPositiveMatchSecret::derive(
		std::span<const uint8_t, FP_POSITIVE_MATCH_SALT_BYTES>(
			input_positive_match_salt.data(),
			FP_POSITIVE_MATCH_SALT_BYTES),
		user_id, tpm_seed);
	if (!secret.has_value()) {
		return secret.error();
	}
	std::copy(secret->bytes().begin(), secret->bytes().end(),
		  output.begin());
	return EC_SUCCESS;
}

[[nodiscard]] inline enum ec_error_list
derive_pairing_key_encryption_key(std::span<uint8_t> output,
				  std::span<const uint8_t> salt)
{
	if (output.size() != sizeof(FpEncryptionKey::Bytes) ||
	    salt.size() != FP_CONTEXT_ENCRYPTION_SALT_BYTES) {
		return EC_ERROR_INVAL;
	}
	const auto key = FpEncryptionKey::derive_for_pairing_key(
		std::span<const uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES>(
			salt.data(), FP_CONTEXT_ENCRYPTION_SALT_BYTES));
	if (!key.has_value()) {
		return key.error();
	}
	std::copy(key->bytes().begin(), key->bytes().end(), output.begin());
	return EC_SUCCESS;
}
#endif

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CRYPTO_H_ */
