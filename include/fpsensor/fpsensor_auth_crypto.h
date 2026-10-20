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
#include <string_view>

/**
 * NIST P-256 elliptic curve key wrapper around BoringSSL EC_KEY.
 */
class FpP256Key {
    public:
	FpP256Key() = default;
	explicit FpP256Key(bssl::UniquePtr<EC_KEY> key) noexcept;

	/** Generate a new random P-256 keypair. */
	[[nodiscard]] static std::expected<FpP256Key, ec_error_list> generate();

	/**
	 * Create a @fp_elliptic_curve_public_key from a 65-byte SEC1
	 * uncompressed elliptic curve point (0x04 || x || y).
	 */
	[[nodiscard]] static std::expected<fp_elliptic_curve_public_key,
					   ec_error_list>
	parse_uncompressed_point(
		std::span<const uint8_t, FP_ASCP_KEY_SIZE> point) noexcept;

	/** Construct a P-256 public key from an EC host command structure. */
	[[nodiscard]] static std::expected<FpP256Key, ec_error_list>
	from_public_key(const fp_elliptic_curve_public_key &pubkey);

	/**
	 * Construct a P-256 public key from a 65-byte SEC1 uncompressed point
	 * (0x04 || x || y).
	 */
	[[nodiscard]] static std::expected<FpP256Key, ec_error_list>
	from_uncompressed_point(
		std::span<const uint8_t, FP_ASCP_KEY_SIZE> point);

	/** Construct a P-256 private key from raw scalar bytes. */
	[[nodiscard]] static std::expected<FpP256Key, ec_error_list>
	from_private_key(std::span<const uint8_t,
				   FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN> privkey);

	/** Export the public key coordinates into an EC host command struct. */
	[[nodiscard]] std::expected<fp_elliptic_curve_public_key, ec_error_list>
	to_public_key() const;

	/**
	 * Generate the ECDH shared secret from this private key and
	 * |peer_public_key| without applying any KDF function on the result.
	 *
	 * IMPORTANT NOTE:
	 * The result is not uniformly distributed, so it should not be used for
	 * anything which requires that property, e.g. symmetric ciphers. The
	 * result should be used as an input to a KDF to produce symmetric key.
	 */
	[[nodiscard]] std::expected<
		CleanseWrapper<std::array<uint8_t, FP_PAIRING_KEY_LEN> >,
		ec_error_list>
	compute_shared_secret(const FpP256Key &peer_public_key) const;

	[[nodiscard]] const EC_KEY *get() const noexcept;

	[[nodiscard]] bssl::UniquePtr<EC_KEY> release() noexcept
	{
		return std::move(key_);
	}

    private:
	bssl::UniquePtr<EC_KEY> key_{};
};

/**
 * Versioned AES-128-GCM authenticated command cipher bound to rollback secret,
 * optional OTP key, TPM seed (when user-bound), and user_id.
 */
class FpAuthCommandCipher {
    public:
	static constexpr uint16_t kSupportedVersion = 1;

	/**
	 * Create fresh encryption metadata (random nonce + salt) for |version|.
	 */
	[[nodiscard]] static std::expected<fp_auth_command_encryption_metadata,
					   ec_error_list>
	create_metadata(uint16_t version = kSupportedVersion);

	/**
	 * Return true if |info| uses a supported metadata version.
	 */
	[[nodiscard]] static bool is_valid_metadata(
		const fp_auth_command_encryption_metadata &info) noexcept;

	/**
	 * Encrypt |data| into |enc_data| with a user-bound key derived from
	 * |user_id| and |tpm_seed|, returning the generated metadata.
	 */
	[[nodiscard]] static std::expected<fp_auth_command_encryption_metadata,
					   ec_error_list>
	encrypt(uint16_t version,
		std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
		std::span<const uint8_t> data, std::span<uint8_t> enc_data);

	/**
	 * Encrypt |data| in-place with a user-bound key derived from |user_id|
	 * and |tpm_seed|, returning the generated metadata.
	 */
	[[nodiscard]] static std::expected<fp_auth_command_encryption_metadata,
					   ec_error_list>
	encrypt_in_place(
		uint16_t version,
		std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
		std::span<uint8_t> data);

	/**
	 * Decrypt |enc_data| into |data| using |info| and a user-bound key
	 * derived from |user_id| and |tpm_seed|.
	 */
	[[nodiscard]] static std::expected<void, ec_error_list>
	decrypt(const fp_auth_command_encryption_metadata &info,
		std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
		std::span<const uint8_t> enc_data, std::span<uint8_t> data);
};

/**
 * 256-bit long-term Pairing Key shared between the FPMCU and the host TA.
 * Automatically cleansed on destruction.
 */
class FpPairingKey {
    public:
	using Bytes = std::array<uint8_t, FP_PAIRING_KEY_LEN>;

	FpPairingKey() noexcept;
	explicit FpPairingKey(const Bytes &bytes) noexcept;

	/**
	 * Establish a new Pairing Key via ECDH between |private_key| and
	 * |peer_pubkey|.
	 */
	[[nodiscard]] static std::expected<FpPairingKey, ec_error_list>
	from_ecdh(const FpP256Key &private_key,
		  const fp_elliptic_curve_public_key &peer_pubkey);

	/**
	 * Unwrap (decrypt) a wrapped Pairing Key bound to rollback secret and
	 * optional OTP key.
	 */
	[[nodiscard]] static std::expected<FpPairingKey, ec_error_list>
	unwrap(const fp_auth_command_encryption_metadata &info,
	       std::span<const uint8_t, FP_PAIRING_KEY_LEN> enc_data);

	/**
	 * Unwrap (decrypt) a wrapped @ec_fp_encrypted_pairing_key structure.
	 */
	[[nodiscard]] static std::expected<FpPairingKey, ec_error_list>
	unwrap(const ec_fp_encrypted_pairing_key &encrypted);

	/**
	 * Wrap (encrypt) this Pairing Key with hardware-bound key material.
	 */
	[[nodiscard]] std::expected<ec_fp_encrypted_pairing_key, ec_error_list>
	wrap(uint16_t version = FP_AES_KEY_ENC_METADATA_VERSION) const;

	[[nodiscard]] const Bytes &bytes() const noexcept;

    private:
	CleanseWrapper<Bytes> key_{};
};

/**
 * 256-bit authenticated session key shared between the FPMCU and the host TA.
 * Automatically cleansed on destruction.
 */
class FpSessionKey {
    public:
	using Bytes = std::array<uint8_t, SHA256_DIGEST_LENGTH>;

	FpSessionKey() noexcept;
	explicit FpSessionKey(const Bytes &bytes) noexcept;

	/**
	 * Derive a session key from FPMCU nonce, peer nonce, Pairing Key, and
	 * optional session context.
	 */
	[[nodiscard]] static std::expected<FpSessionKey, ec_error_list>
	derive(std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	       std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce,
	       std::span<const uint8_t, FP_PAIRING_KEY_LEN> pairing_key,
	       std::span<const uint8_t> session_context = {});

	/**
	 * Decrypt |input| into |output| using AES-256-GCM with this session
	 * key.
	 */
	[[nodiscard]] std::expected<void, ec_error_list>
	decrypt(std::span<const uint8_t> input, std::span<uint8_t> output,
		std::span<const uint8_t, FP_AES_KEY_NONCE_BYTES> nonce,
		std::span<const uint8_t, FP_AES_KEY_TAG_BYTES> tag,
		std::string_view aad) const;

	/**
	 * Compute an HMAC-SHA256 message signature over |challenge|, |context|,
	 * |sender|, and |operation|.
	 */
	[[nodiscard]] std::expected<std::array<uint8_t, SHA256_DIGEST_LENGTH>,
				    ec_error_list>
	sign(std::span<const uint8_t> context, std::string_view sender,
	     std::string_view operation,
	     std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge) const;

	/**
	 * Verify an HMAC-SHA256 message signature in constant time.
	 */
	[[nodiscard]] std::expected<void, ec_error_list>
	verify(std::span<const uint8_t> context, std::string_view sender,
	       std::string_view operation,
	       std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge,
	       std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac) const;

	void clear() noexcept;

	[[nodiscard]] const Bytes &bytes() const noexcept;

    private:
	CleanseWrapper<Bytes> key_{};
};

#ifndef CONFIG_ZEPHYR
#include "openssl/ecdh.h"

#include <algorithm>
#include <optional>
#include <utility>

[[nodiscard]] inline std::optional<fp_elliptic_curve_public_key>
create_pubkey_from_ec_key(const EC_KEY &key)
{
	uint8_t *data = nullptr;
	if (EC_KEY_key2buf(&key, POINT_CONVERSION_UNCOMPRESSED, &data,
			   nullptr) != FP_ASCP_KEY_SIZE) {
		return std::nullopt;
	}
	bssl::UniquePtr<uint8_t> pubkey_data(data);
	const auto res = FpP256Key::parse_uncompressed_point(
		std::span<const uint8_t, FP_ASCP_KEY_SIZE>(pubkey_data.get(),
							   FP_ASCP_KEY_SIZE));
	if (!res.has_value()) {
		return std::nullopt;
	}
	return *res;
}

[[nodiscard]] inline bssl::UniquePtr<EC_KEY>
create_ec_key_from_pubkey(const fp_elliptic_curve_public_key &pubkey)
{
	auto res = FpP256Key::from_public_key(pubkey);
	if (!res.has_value()) {
		return nullptr;
	}
	return res->release();
}

[[nodiscard]] inline bssl::UniquePtr<EC_KEY>
create_ec_key_from_privkey(const uint8_t *privkey, size_t privkey_size)
{
	if (privkey == nullptr ||
	    privkey_size != FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN) {
		return nullptr;
	}
	auto res = FpP256Key::from_private_key(
		std::span<const uint8_t, FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN>(
			privkey, FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN));
	if (!res.has_value()) {
		return nullptr;
	}
	return res->release();
}

inline enum ec_error_list
encrypt_data(uint16_t version, struct fp_auth_command_encryption_metadata &info,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t> data, std::span<uint8_t> enc_data)
{
	const auto res = FpAuthCommandCipher::encrypt(version, user_id,
						      tpm_seed, data, enc_data);
	if (!res.has_value()) {
		return res.error();
	}
	info = *res;
	return EC_SUCCESS;
}

inline enum ec_error_list
encrypt_data_in_place(uint16_t version,
		      struct fp_auth_command_encryption_metadata &info,
		      std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		      std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
		      std::span<uint8_t> data)
{
	return encrypt_data(version, info, user_id, tpm_seed, data, data);
}

[[nodiscard]] inline enum ec_error_list
encrypt_pairing_key(uint16_t version,
		    struct fp_auth_command_encryption_metadata &info,
		    std::span<const uint8_t, FP_PAIRING_KEY_LEN> data,
		    std::span<uint8_t, FP_PAIRING_KEY_LEN> enc_data)
{
	FpPairingKey::Bytes bytes{};
	std::copy(data.begin(), data.end(), bytes.begin());
	const auto wrapped = FpPairingKey(bytes).wrap(version);
	if (!wrapped.has_value()) {
		return wrapped.error();
	}
	info = wrapped->info;
	std::copy(std::begin(wrapped->data), std::end(wrapped->data),
		  enc_data.begin());
	return EC_SUCCESS;
}

[[nodiscard]] inline enum ec_error_list
decrypt_data(const struct fp_auth_command_encryption_metadata &info,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t> enc_data, std::span<uint8_t> data)
{
	return FpAuthCommandCipher::decrypt(info, user_id, tpm_seed, enc_data,
					    data)
		.error_or(EC_SUCCESS);
}

[[nodiscard]] inline enum ec_error_list
decrypt_pairing_key(const struct fp_auth_command_encryption_metadata &info,
		    std::span<const uint8_t, FP_PAIRING_KEY_LEN> enc_data,
		    std::span<uint8_t, FP_PAIRING_KEY_LEN> data)
{
	const auto key = FpPairingKey::unwrap(info, enc_data);
	if (!key.has_value()) {
		return key.error();
	}
	std::copy(key->bytes().begin(), key->bytes().end(), data.begin());
	return EC_SUCCESS;
}

[[nodiscard]] inline enum ec_error_list
generate_ecdh_shared_secret(const EC_KEY &private_key, const EC_KEY &public_key,
			    std::span<uint8_t> secret)
{
	const EC_POINT *public_point = EC_KEY_get0_public_key(&public_key);
	if (public_point == nullptr) {
		return EC_ERROR_INVAL;
	}
	if (ECDH_compute_key_fips(secret.data(), secret.size(), public_point,
				  &private_key) != 1) {
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}

[[nodiscard]] inline enum ec_error_list
generate_ecdh_shared_secret_without_kdf(const EC_KEY &private_key,
					const EC_KEY &public_key,
					std::span<uint8_t> secret)
{
	const EC_POINT *public_point = EC_KEY_get0_public_key(&public_key);
	if (public_point == nullptr) {
		return EC_ERROR_INVAL;
	}
	if (ECDH_compute_key(secret.data(), secret.size(), public_point,
			     &private_key, nullptr) == -1) {
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}

[[nodiscard]] inline enum ec_error_list generate_session_key(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce,
	std::span<const uint8_t, FP_PAIRING_KEY_LEN> pairing_key,
	std::span<const uint8_t> session_context,
	std::span<uint8_t, SHA256_DIGEST_LENGTH> session_key)
{
	const auto key = FpSessionKey::derive(fpmcu_nonce, peer_nonce,
					      pairing_key, session_context);
	if (!key.has_value()) {
		return key.error();
	}
	std::copy(key->bytes().begin(), key->bytes().end(),
		  session_key.begin());
	return EC_SUCCESS;
}

[[nodiscard]] inline enum ec_error_list decrypt_data_with_session_key(
	std::span<const uint8_t, 32> session_key,
	std::span<const uint8_t> input, std::span<uint8_t> output,
	std::span<const uint8_t, FP_AES_KEY_NONCE_BYTES> nonce,
	std::span<const uint8_t, FP_AES_KEY_TAG_BYTES> tag,
	std::span<const uint8_t> aad)
{
	FpSessionKey::Bytes bytes{};
	std::copy(session_key.begin(), session_key.end(), bytes.begin());
	return FpSessionKey(bytes)
		.decrypt(input, output, nonce, tag,
			 std::string_view(
				 reinterpret_cast<const char *>(aad.data()),
				 aad.size()))
		.error_or(EC_SUCCESS);
}

[[nodiscard]] inline enum ec_error_list compute_message_signature(
	std::span<const uint8_t, SHA256_DIGEST_LENGTH> session_key,
	std::span<const uint8_t> context, std::span<const uint8_t> sender,
	std::span<const uint8_t> operation,
	std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge,
	std::span<uint8_t, SHA256_DIGEST_LENGTH> signature)
{
	FpSessionKey::Bytes bytes{};
	std::copy(session_key.begin(), session_key.end(), bytes.begin());
	const auto sig = FpSessionKey(bytes).sign(
		context,
		std::string_view(reinterpret_cast<const char *>(sender.data()),
				 sender.size()),
		std::string_view(
			reinterpret_cast<const char *>(operation.data()),
			operation.size()),
		challenge);
	if (!sig.has_value()) {
		return sig.error();
	}
	std::copy(sig->begin(), sig->end(), signature.begin());
	return EC_SUCCESS;
}
#endif

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_AUTH_CRYPTO_H_ */
