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

#include <stdbool.h>

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

#if !defined(CONFIG_BORINGSSL_CRYPTO) || !defined(CONFIG_ROLLBACK_SECRET_SIZE)
#error "fpsensor requires CONFIG_BORINGSSL_CRYPTO and ROLLBACK_SECRET_SIZE"
#endif

enum ec_error_list
hmac_sha256(std::span<const uint8_t> key,
	    std::span<const std::span<const uint8_t> > inputs,
	    std::span<uint8_t, SHA256_DIGEST_LENGTH> output)
{
	bssl::ScopedHMAC_CTX ctx;

	if (!HMAC_Init_ex(ctx.get(), key.data(), key.size(), EVP_sha256(),
			  nullptr)) {
		return EC_ERROR_INVAL;
	}

	for (const auto &input : inputs) {
		if (!HMAC_Update(ctx.get(), input.data(), input.size())) {
			return EC_ERROR_INVAL;
		}
	}

	if (!HMAC_Final(ctx.get(), output.data(), nullptr)) {
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static enum ec_error_list
hkdf_sha256_extract(std::span<uint8_t, SHA256_DIGEST_LENGTH> prk,
		    std::span<const std::span<const uint8_t> > ikms,
		    std::span<const uint8_t> salt)
{
	/*
	 * As specified by RFC5869, the extract step of HKDF is HMAC of IKM
	 * (Input Key Material) with salt used as the key. The output of HMAC
	 * is PRK (Pseudo Random Key).
	 */
	return hmac_sha256(salt, ikms, prk);
}

bool hkdf_sha256_impl(std::span<uint8_t> out_key,
		      std::span<const std::span<const uint8_t> > ikms,
		      std::span<const uint8_t> salt,
		      std::span<const uint8_t> info)
{
	CleanseWrapper<std::array<uint8_t, SHA256_DIGEST_LENGTH> > prk;

	if (hkdf_sha256_extract(prk, ikms, salt) != EC_SUCCESS) {
		return false;
	}

	return HKDF_expand(out_key.data(), out_key.size(), EVP_sha256(),
			   prk.data(), prk.size(), info.data(), info.size());
}

test_mockable bool hkdf_sha256(std::span<uint8_t> out_key,
			       std::span<const std::span<const uint8_t> > ikms,
			       std::span<const uint8_t> salt,
			       std::span<const uint8_t> info)
{
	return hkdf_sha256_impl(out_key, ikms, salt, info);
}

test_export_static enum ec_error_list
get_rollback_entropy(std::span<uint8_t, CONFIG_ROLLBACK_SECRET_SIZE> output)
{
	enum ec_error_list ret = rollback_get_secret(output.data());

	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to read rollback secret: %d", ret);
		return EC_ERROR_HW_INTERNAL;
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_OTP_KEY
test_export_static enum ec_error_list
get_otp_key(std::span<uint8_t, OTP_KEY_SIZE_BYTES> output)
{
	enum ec_error_list ret;

	otp_key_init();
	ret = (enum ec_error_list)otp_key_read(output.data());
	otp_key_exit();

	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to read OTP key with ret=%d", ret);
		return EC_ERROR_HW_INTERNAL;
	}

	if (bytes_are_trivial(output)) {
		CPRINTS("ERROR: bytes read from OTP are trivial!");
		return EC_ERROR_HW_INTERNAL;
	}

	return EC_SUCCESS;
}
#endif

static enum ec_error_list derive_key(std::span<uint8_t> output,
				     std::span<const uint8_t> salt,
				     std::span<const uint8_t> tpm_seed,
				     std::span<const uint8_t> info)
{
	CleanseWrapper<std::array<uint8_t, CONFIG_ROLLBACK_SECRET_SIZE> >
		rollback_entropy;
#ifdef CONFIG_OTP_KEY
	CleanseWrapper<std::array<uint8_t, OTP_KEY_SIZE_BYTES> > otp_key;
#endif
	enum ec_error_list ret;

	if (!tpm_seed.empty() && tpm_seed.size() != FP_CONTEXT_TPM_BYTES) {
		return EC_ERROR_INVAL;
	}

	/* Make sure TPM Seed is set */
	if (!tpm_seed.empty() && bytes_are_trivial(tpm_seed)) {
		CPRINTS("Seed hasn't been set.");
		return EC_ERROR_ACCESS_DENIED;
	}

	ret = get_rollback_entropy(rollback_entropy);
	if (ret != EC_SUCCESS) {
		return ret;
	}

#ifdef CONFIG_OTP_KEY
	ret = get_otp_key(otp_key);
	if (ret != EC_SUCCESS) {
		return ret;
	}
#endif

	/*
	 * The IKM consists of rollback entropy, TPM Seed (if provided) and
	 * optional OTP key.
	 *
	 * By default, the compiler deduces static extent from built-in arrays
	 * and std::array, but in the ikms array we can only keep spans with
	 * dynamic extent. Prevent compiler from deducing static extent by
	 * providing template arguments (std::dynamic_extent is the default
	 * value for 'Extent' parameter, so we don't need to specify it
	 * explicitly). See C++ std::span deduction guide for more details.
	 */
	std::array ikms{
		std::span<const uint8_t>{ rollback_entropy },
		tpm_seed,
#ifdef CONFIG_OTP_KEY
		std::span<const uint8_t>{ otp_key },
#endif
	};

	if (!hkdf_sha256(output, ikms, salt, info)) {
		CPRINTS("Failed to perform HKDF");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

enum ec_error_list derive_positive_match_secret(
	std::span<uint8_t> output,
	std::span<const uint8_t> input_positive_match_salt,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	static constexpr std::string_view info_prefix =
		"positive_match_secret for user ";
	std::array<uint8_t, info_prefix.size() + user_id.size_bytes()> info{};

	if (bytes_are_trivial(input_positive_match_salt)) {
		CPRINTS("Failed to derive positive match secret: "
			"salt bytes are trivial.");
		return EC_ERROR_INVAL;
	}

	auto out = std::ranges::copy(info_prefix, info.begin()).out;
	std::ranges::copy(user_id, out);

	enum ec_error_list ret =
		derive_key(output, input_positive_match_salt, tpm_seed, info);
	if (ret != EC_SUCCESS) {
		return ret;
	}

	/* Check that secret is not full of 0x00 or 0xff. */
	if (bytes_are_trivial(output)) {
		CPRINTS("Failed to derive positive match secret: "
			"derived secret bytes are trivial.");
		ret = EC_ERROR_HW_INTERNAL;
	}
	return ret;
}

enum ec_error_list
derive_encryption_key(std::span<uint8_t> out_key, std::span<const uint8_t> salt,
		      std::span<const uint8_t> info,
		      std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	if (info.size() != SHA256_DIGEST_LENGTH) {
		CPRINTS("Invalid info size: %zu", info.size());
		return EC_ERROR_INVAL;
	}

	return derive_key(out_key, salt, tpm_seed, info);
}

enum ec_error_list
derive_pairing_key_encryption_key(std::span<uint8_t> output,
				  std::span<const uint8_t> salt)
{
	static constexpr uint8_t info[] = "FPMCU & FingerGuard pairing key";

	return derive_key(output, salt, std::span<const uint8_t>{}, info);
}

enum ec_error_list aes_128_gcm_encrypt(std::span<const uint8_t> key,
				       std::span<const uint8_t> plaintext,
				       std::span<uint8_t> ciphertext,
				       std::span<const uint8_t> nonce,
				       std::span<uint8_t> tag)
{
	if (nonce.size() != FP_CONTEXT_NONCE_BYTES) {
		CPRINTS("Invalid nonce size %zu bytes", nonce.size());
		return EC_ERROR_INVAL;
	}

	bssl::ScopedEVP_AEAD_CTX ctx;
	int ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_128_gcm(),
				    key.data(), key.size(), tag.size(),
				    nullptr);
	if (!ret) {
		CPRINTS("Failed to initialize encryption context");
		return EC_ERROR_UNKNOWN;
	}

	size_t out_tag_size = 0;
	std::span<uint8_t> extra_input; /* no extra input */
	std::span<uint8_t> additional_data; /* no additional data */
	ret = EVP_AEAD_CTX_seal_scatter(
		ctx.get(), ciphertext.data(), tag.data(), &out_tag_size,
		tag.size(), nonce.data(), nonce.size(), plaintext.data(),
		plaintext.size(), extra_input.data(), extra_input.size(),
		additional_data.data(), additional_data.size());
	if (!ret) {
		CPRINTS("Failed to encrypt");
		return EC_ERROR_UNKNOWN;
	}
	if (out_tag_size != tag.size()) {
		CPRINTS("Resulting tag size %zu does not match expected size: %zu",
			out_tag_size, tag.size());
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

enum ec_error_list aes_128_gcm_decrypt(std::span<const uint8_t> key,
				       std::span<uint8_t> plaintext,
				       std::span<const uint8_t> ciphertext,
				       std::span<const uint8_t> nonce,
				       std::span<const uint8_t> tag)
{
	if (nonce.size() != FP_CONTEXT_NONCE_BYTES) {
		CPRINTS("Invalid nonce size %zu bytes", nonce.size());
		return EC_ERROR_INVAL;
	}

	bssl::ScopedEVP_AEAD_CTX ctx;
	int ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_128_gcm(),
				    key.data(), key.size(), tag.size(),
				    nullptr);
	if (!ret) {
		CPRINTS("Failed to initialize encryption context");
		return EC_ERROR_UNKNOWN;
	}

	std::span<uint8_t> additional_data; /* no additional data */
	ret = EVP_AEAD_CTX_open_gather(
		ctx.get(), plaintext.data(), nonce.data(), nonce.size(),
		ciphertext.data(), ciphertext.size(), tag.data(), tag.size(),
		additional_data.data(), additional_data.size());
	if (!ret) {
		CPRINTS("Failed to decrypt");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}
