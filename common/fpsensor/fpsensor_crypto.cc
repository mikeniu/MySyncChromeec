/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto/cleanse_wrapper.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_utils.h"
#include "openssl/aead.h"
#include "openssl/evp.h"
#include "openssl/hkdf.h"
#include "openssl/hmac.h"
#include "openssl/mem.h"
#include "openssl/sha.h"
#include "otp_key.h"
#include "rollback.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <bit>
#include <span>
#include <string_view>

#pragma GCC poison global_context

#if !defined(CONFIG_BORINGSSL_CRYPTO) || !defined(CONFIG_ROLLBACK_SECRET_SIZE)
#error "fpsensor requires CONFIG_BORINGSSL_CRYPTO and ROLLBACK_SECRET_SIZE"
#endif

namespace
{

class FpHmacSha256 {
    public:
	using Digest = std::array<uint8_t, SHA256_DIGEST_LENGTH>;

	FpHmacSha256() = default;

	[[nodiscard]] std::expected<void, ec_error_list>
	init(std::span<const uint8_t> key)
	{
		if (!HMAC_Init_ex(ctx_.get(), key.data(), key.size(),
				  EVP_sha256(), nullptr)) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		return {};
	}

	[[nodiscard]] std::expected<void, ec_error_list>
	update(std::span<const uint8_t> data)
	{
		if (!HMAC_Update(ctx_.get(), data.data(), data.size())) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		return {};
	}

	[[nodiscard]] std::expected<Digest, ec_error_list> finalize()
	{
		Digest output{};
		if (!HMAC_Final(ctx_.get(), output.data(), nullptr)) {
			return std::unexpected(EC_ERROR_INVAL);
		}
		return output;
	}

	[[nodiscard]] static std::expected<Digest, ec_error_list>
	compute(std::span<const uint8_t> key,
		std::span<const std::span<const uint8_t> > inputs)
	{
		FpHmacSha256 hmac;
		if (const auto res = hmac.init(key); !res.has_value()) {
			return std::unexpected(res.error());
		}
		for (const auto &input : inputs) {
			if (const auto res = hmac.update(input);
			    !res.has_value()) {
				return std::unexpected(res.error());
			}
		}
		return hmac.finalize();
	}

    private:
	bssl::ScopedHMAC_CTX ctx_{};
};

} /* namespace */

std::expected<std::array<uint8_t, SHA256_DIGEST_LENGTH>, ec_error_list>
hmac_sha256(std::span<const uint8_t> key,
	    std::span<const std::span<const uint8_t> > inputs)
{
	return FpHmacSha256::compute(key, inputs);
}

namespace
{

std::expected<CleanseWrapper<std::array<uint8_t, SHA256_DIGEST_LENGTH> >,
	      ec_error_list>
hkdf_sha256_extract(std::span<const std::span<const uint8_t> > ikms,
		    std::span<const uint8_t> salt)
{
	/*
	 * As specified by RFC5869, the extract step of HKDF is HMAC of IKM
	 * (Input Key Material) with salt used as the key. The output of HMAC
	 * is PRK (Pseudo Random Key).
	 */
	return hmac_sha256(salt, ikms).transform([](const auto &prk) {
		return CleanseWrapper(prk);
	});
}

} /* namespace */

bool hkdf_sha256_impl(std::span<uint8_t> out_key,
		      std::span<const std::span<const uint8_t> > ikms,
		      std::span<const uint8_t> salt,
		      std::span<const uint8_t> info)
{
	const auto prk = hkdf_sha256_extract(ikms, salt);
	if (!prk.has_value()) {
		return false;
	}

	return HKDF_expand(out_key.data(), out_key.size(), EVP_sha256(),
			   prk->data(), prk->size(), info.data(),
			   info.size()) == 1;
}

test_mockable bool hkdf_sha256(std::span<uint8_t> out_key,
			       std::span<const std::span<const uint8_t> > ikms,
			       std::span<const uint8_t> salt,
			       std::span<const uint8_t> info)
{
	return hkdf_sha256_impl(out_key, ikms, salt, info);
}

namespace
{

std::expected<CleanseWrapper<std::array<uint8_t, CONFIG_ROLLBACK_SECRET_SIZE> >,
	      ec_error_list>
get_rollback_entropy()
{
	CleanseWrapper<std::array<uint8_t, CONFIG_ROLLBACK_SECRET_SIZE> > output;
	const ec_error_list ret = rollback_get_secret(output.data());

	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to read rollback secret: %d", ret);
		return std::unexpected(EC_ERROR_HW_INTERNAL);
	}

	return output;
}

#ifdef CONFIG_OTP_KEY
class ScopedOtpKey {
    public:
	ScopedOtpKey()
	{
		otp_key_init();
	}

	~ScopedOtpKey()
	{
		otp_key_exit();
	}

	ScopedOtpKey(const ScopedOtpKey &) = delete;
	ScopedOtpKey &operator=(const ScopedOtpKey &) = delete;

	[[nodiscard]] ec_error_list
	read(std::span<uint8_t, OTP_KEY_SIZE_BYTES> out) const
	{
		return static_cast<ec_error_list>(otp_key_read(out.data()));
	}
};

std::expected<CleanseWrapper<std::array<uint8_t, OTP_KEY_SIZE_BYTES> >,
	      ec_error_list>
get_otp_key()
{
	CleanseWrapper<std::array<uint8_t, OTP_KEY_SIZE_BYTES> > output;
	const ec_error_list ret = ScopedOtpKey{}.read(output);

	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to read OTP key with ret=%d", ret);
		return std::unexpected(EC_ERROR_HW_INTERNAL);
	}

	if (bytes_are_trivial(output)) {
		CPRINTS("ERROR: bytes read from OTP are trivial!");
		return std::unexpected(EC_ERROR_HW_INTERNAL);
	}

	return output;
}
#endif

class FpHardwareKeyMaterial {
    public:
	[[nodiscard]] static std::expected<FpHardwareKeyMaterial, ec_error_list>
	gather(std::span<const uint8_t> tpm_seed = {})
	{
		if (!tpm_seed.empty() &&
		    tpm_seed.size() != FP_CONTEXT_TPM_BYTES) {
			return std::unexpected(EC_ERROR_INVAL);
		}

		/* Make sure TPM Seed is set */
		if (!tpm_seed.empty() && bytes_are_trivial(tpm_seed)) {
			CPRINTS("Seed hasn't been set.");
			return std::unexpected(EC_ERROR_ACCESS_DENIED);
		}

		const auto rollback_entropy = get_rollback_entropy();
		if (!rollback_entropy.has_value()) {
			return std::unexpected(rollback_entropy.error());
		}

#ifdef CONFIG_OTP_KEY
		const auto otp_key = get_otp_key();
		if (!otp_key.has_value()) {
			return std::unexpected(otp_key.error());
		}
		return FpHardwareKeyMaterial(*rollback_entropy, tpm_seed,
					     *otp_key);
#else
		return FpHardwareKeyMaterial(*rollback_entropy, tpm_seed);
#endif
	}

	template <size_t N>
	[[nodiscard]] std::expected<CleanseWrapper<std::array<uint8_t, N> >,
				    ec_error_list>
	derive_hkdf(std::span<const uint8_t> salt,
		    std::span<const uint8_t> info) const
	{
		/*
		 * The IKM consists of rollback entropy, TPM Seed (if provided)
		 * and optional OTP key.
		 *
		 * By default, the compiler deduces static extent from built-in
		 * arrays and std::array, but in the ikms array we can only keep
		 * spans with dynamic extent. Prevent compiler from deducing
		 * static extent by providing template arguments
		 * (std::dynamic_extent is the default value for 'Extent'
		 * parameter, so we don't need to specify it explicitly). See
		 * C++ std::span deduction guide for more details.
		 */
		std::array ikms{
			std::span<const uint8_t>{ rollback_entropy_ },
			tpm_seed_,
#ifdef CONFIG_OTP_KEY
			std::span<const uint8_t>{ otp_key_ },
#endif
		};

		CleanseWrapper<std::array<uint8_t, N> > output;
		if (!hkdf_sha256(output, ikms, salt, info)) {
			CPRINTS("Failed to perform HKDF");
			return std::unexpected(EC_ERROR_UNKNOWN);
		}

		return output;
	}

    private:
#ifdef CONFIG_OTP_KEY
	FpHardwareKeyMaterial(
		const CleanseWrapper<
			std::array<uint8_t, CONFIG_ROLLBACK_SECRET_SIZE> >
			&rollback_entropy,
		std::span<const uint8_t> tpm_seed,
		const CleanseWrapper<std::array<uint8_t, OTP_KEY_SIZE_BYTES> > &
			otp_key) noexcept : rollback_entropy_(rollback_entropy),
					    tpm_seed_(tpm_seed),
					    otp_key_(otp_key)
	{
	}
#else
	FpHardwareKeyMaterial(
		const CleanseWrapper<
			std::array<uint8_t, CONFIG_ROLLBACK_SECRET_SIZE> >
			&rollback_entropy,
		std::span<const uint8_t> tpm_seed) noexcept
		: rollback_entropy_(rollback_entropy),
		  tpm_seed_(tpm_seed)
	{
	}
#endif

	CleanseWrapper<std::array<uint8_t, CONFIG_ROLLBACK_SECRET_SIZE> >
		rollback_entropy_{};
	std::span<const uint8_t> tpm_seed_{};
#ifdef CONFIG_OTP_KEY
	CleanseWrapper<std::array<uint8_t, OTP_KEY_SIZE_BYTES> > otp_key_{};
#endif
};

template <size_t N>
std::expected<CleanseWrapper<std::array<uint8_t, N> >, ec_error_list>
derive_key(std::span<const uint8_t> salt, std::span<const uint8_t> tpm_seed,
	   std::span<const uint8_t> info)
{
	return FpHardwareKeyMaterial::gather(tpm_seed).and_then(
		[&](const FpHardwareKeyMaterial &ikm) {
			return ikm.derive_hkdf<N>(salt, info);
		});
}

} /* namespace */

#if !defined(CONFIG_ZEPHYR) && defined(TEST_BUILD)
enum ec_error_list
get_rollback_entropy(std::span<uint8_t, CONFIG_ROLLBACK_SECRET_SIZE> output)
{
	const auto res = get_rollback_entropy();
	if (!res.has_value()) {
		return res.error();
	}
	std::ranges::copy(*res, output.begin());
	return EC_SUCCESS;
}

#ifdef CONFIG_OTP_KEY
enum ec_error_list get_otp_key(std::span<uint8_t, OTP_KEY_SIZE_BYTES> output)
{
	const auto res = get_otp_key();
	if (!res.has_value()) {
		return res.error();
	}
	std::ranges::copy(*res, output.begin());
	return EC_SUCCESS;
}
#endif
#endif

FpPositiveMatchSecret::FpPositiveMatchSecret() noexcept : secret_(Bytes{})
{
}

FpPositiveMatchSecret::FpPositiveMatchSecret(const Bytes &bytes) noexcept
	: secret_(bytes)
{
}

const FpPositiveMatchSecret::Bytes &
FpPositiveMatchSecret::bytes() const noexcept
{
	return secret_;
}

std::expected<FpPositiveMatchSecret, ec_error_list>
FpPositiveMatchSecret::derive(
	std::span<const uint8_t, FP_POSITIVE_MATCH_SALT_BYTES>
		input_positive_match_salt,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	static constexpr std::string_view kInfoPrefix =
		"positive_match_secret for user ";
	std::array<uint8_t, kInfoPrefix.size() + user_id.size_bytes()> info{};

	if (bytes_are_trivial(input_positive_match_salt)) {
		CPRINTS("Failed to derive positive match secret: "
			"salt bytes are trivial.");
		return std::unexpected(EC_ERROR_INVAL);
	}

	auto out = std::ranges::copy(kInfoPrefix, info.begin()).out;
	std::ranges::copy(user_id, out);

	const auto output = derive_key<FP_POSITIVE_MATCH_SECRET_BYTES>(
		input_positive_match_salt, tpm_seed, info);
	if (!output.has_value()) {
		return std::unexpected(output.error());
	}

	/* Check that secret is not full of 0x00 or 0xff. */
	if (bytes_are_trivial(*output)) {
		CPRINTS("Failed to derive positive match secret: "
			"derived secret bytes are trivial.");
		return std::unexpected(EC_ERROR_HW_INTERNAL);
	}
	return FpPositiveMatchSecret(*output);
}

FpEncryptionKey::FpEncryptionKey() noexcept : key_(Bytes{})
{
}

FpEncryptionKey::FpEncryptionKey(const Bytes &bytes) noexcept : key_(bytes)
{
}

const FpEncryptionKey::Bytes &FpEncryptionKey::bytes() const noexcept
{
	return key_;
}

std::expected<FpEncryptionKey, ec_error_list> FpEncryptionKey::derive(
	std::span<const uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES> salt,
	std::span<const uint8_t> info,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	if (info.size() != SHA256_DIGEST_LENGTH) {
		CPRINTS("Invalid info size: %zu", info.size());
		return std::unexpected(EC_ERROR_INVAL);
	}

	return derive_key<sizeof(Bytes)>(salt, tpm_seed, info)
		.transform([](const auto &bytes) {
			return FpEncryptionKey(bytes);
		});
}

std::expected<FpEncryptionKey, ec_error_list>
FpEncryptionKey::derive_for_pairing_key(
	std::span<const uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES> salt)
{
	static constexpr auto kPairingKeyInfo =
		std::bit_cast<std::array<uint8_t, 32> >(
			std::to_array("FPMCU & FingerGuard pairing key"));

	return derive_key<sizeof(Bytes)>(salt, std::span<const uint8_t>{},
					 kPairingKeyInfo)
		.transform([](const auto &bytes) {
			return FpEncryptionKey(bytes);
		});
}

std::expected<void, ec_error_list> FpEncryptionKey::encrypt(
	std::span<const uint8_t> plaintext, std::span<uint8_t> ciphertext,
	std::span<const uint8_t> nonce, std::span<uint8_t> tag) const
{
	if (const ec_error_list err = aes_128_gcm_encrypt(
		    key_, plaintext, ciphertext, nonce, tag);
	    err != EC_SUCCESS) {
		return std::unexpected(err);
	}
	return {};
}

std::expected<void, ec_error_list> FpEncryptionKey::decrypt(
	std::span<uint8_t> plaintext, std::span<const uint8_t> ciphertext,
	std::span<const uint8_t> nonce, std::span<const uint8_t> tag) const
{
	if (const ec_error_list err = aes_128_gcm_decrypt(
		    key_, plaintext, ciphertext, nonce, tag);
	    err != EC_SUCCESS) {
		return std::unexpected(err);
	}
	return {};
}

namespace
{

class FpAes128Gcm {
    public:
	FpAes128Gcm() = default;

	[[nodiscard]] std::expected<void, ec_error_list>
	init(std::span<const uint8_t> key, std::span<const uint8_t> nonce,
	     size_t tag_size)
	{
		if (nonce.size() != FP_CONTEXT_NONCE_BYTES) {
			CPRINTS("Invalid nonce size %zu bytes", nonce.size());
			return std::unexpected(EC_ERROR_INVAL);
		}

		if (!EVP_AEAD_CTX_init(ctx_.get(), EVP_aead_aes_128_gcm(),
				       key.data(), key.size(), tag_size,
				       nullptr)) {
			CPRINTS("Failed to initialize encryption context");
			return std::unexpected(EC_ERROR_UNKNOWN);
		}

		nonce_ = nonce;
		return {};
	}

	[[nodiscard]] std::expected<void, ec_error_list>
	seal(std::span<const uint8_t> plaintext, std::span<uint8_t> ciphertext,
	     std::span<uint8_t> tag) const
	{
		size_t out_tag_size = 0;
		if (!EVP_AEAD_CTX_seal_scatter(
			    ctx_.get(), ciphertext.data(), tag.data(),
			    &out_tag_size, tag.size(), nonce_.data(),
			    nonce_.size(), plaintext.data(), plaintext.size(),
			    nullptr, 0, nullptr, 0)) {
			CPRINTS("Failed to encrypt");
			return std::unexpected(EC_ERROR_UNKNOWN);
		}
		if (out_tag_size != tag.size()) {
			CPRINTS("Resulting tag size %zu does not match expected size: %zu",
				out_tag_size, tag.size());
			return std::unexpected(EC_ERROR_UNKNOWN);
		}
		return {};
	}

	[[nodiscard]] std::expected<void, ec_error_list>
	open(std::span<const uint8_t> ciphertext, std::span<uint8_t> plaintext,
	     std::span<const uint8_t> tag) const
	{
		if (!EVP_AEAD_CTX_open_gather(
			    ctx_.get(), plaintext.data(), nonce_.data(),
			    nonce_.size(), ciphertext.data(), ciphertext.size(),
			    tag.data(), tag.size(), nullptr, 0)) {
			CPRINTS("Failed to decrypt");
			return std::unexpected(EC_ERROR_UNKNOWN);
		}
		return {};
	}

    private:
	bssl::ScopedEVP_AEAD_CTX ctx_{};
	std::span<const uint8_t> nonce_{};
};

} /* namespace */

enum ec_error_list aes_128_gcm_encrypt(std::span<const uint8_t> key,
				       std::span<const uint8_t> plaintext,
				       std::span<uint8_t> ciphertext,
				       std::span<const uint8_t> nonce,
				       std::span<uint8_t> tag)
{
	FpAes128Gcm cipher;
	return cipher.init(key, nonce, tag.size())
		.and_then(
			[&] { return cipher.seal(plaintext, ciphertext, tag); })
		.error_or(EC_SUCCESS);
}

enum ec_error_list aes_128_gcm_decrypt(std::span<const uint8_t> key,
				       std::span<uint8_t> plaintext,
				       std::span<const uint8_t> ciphertext,
				       std::span<const uint8_t> nonce,
				       std::span<const uint8_t> tag)
{
	FpAes128Gcm cipher;
	return cipher.init(key, nonce, tag.size())
		.and_then(
			[&] { return cipher.open(ciphertext, plaintext, tag); })
		.error_or(EC_SUCCESS);
}
