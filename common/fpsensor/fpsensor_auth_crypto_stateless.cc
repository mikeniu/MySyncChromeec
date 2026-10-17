/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_crypto.h"
#include "openssl/aead.h"
#include "openssl/aes.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/ecdh.h"
#include "openssl/obj_mac.h"
#include "openssl/rand.h"

#include <algorithm>
#include <array>

std::expected<fp_elliptic_curve_public_key, ec_error_list>
create_pubkey_from_uncompressed_point(
	std::span<const uint8_t, FP_ASCP_KEY_SIZE> point) noexcept
{
	/* POINT_CONVERSION_UNCOMPRESSED indicates that the point is encoded as
	 * z||x||y, where z is the octet 0x04. */
	if (point[0] != POINT_CONVERSION_UNCOMPRESSED) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	fp_elliptic_curve_public_key pubkey{};
	static_assert(point.size() == 1 + sizeof(pubkey.x) + sizeof(pubkey.y));
	std::ranges::copy(point.subspan<1, sizeof(pubkey.x)>(), pubkey.x);
	std::ranges::copy(
		point.subspan<1 + sizeof(pubkey.x), sizeof(pubkey.y)>(),
		pubkey.y);
	return pubkey;
}

std::expected<fp_elliptic_curve_public_key, ec_error_list>
create_pubkey_from_ec_key(const EC_KEY &key)
{
	uint8_t *data = nullptr;
	if (EC_KEY_key2buf(&key, POINT_CONVERSION_UNCOMPRESSED, &data,
			   nullptr) != FP_ASCP_KEY_SIZE) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	bssl::UniquePtr<uint8_t> pubkey_data(data);
	return create_pubkey_from_uncompressed_point(
		std::span<const uint8_t, FP_ASCP_KEY_SIZE>(pubkey_data.get(),
							   FP_ASCP_KEY_SIZE));
}

namespace
{

std::expected<bssl::UniquePtr<EC_KEY>, ec_error_list> new_p256_key()
{
	bssl::UniquePtr<EC_KEY> key(
		EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
	if (key == nullptr) {
		return std::unexpected(EC_ERROR_MEMORY_ALLOCATION);
	}
	return key;
}

} /* namespace */

std::expected<bssl::UniquePtr<EC_KEY>, ec_error_list>
create_ec_key_from_pubkey(const fp_elliptic_curve_public_key &pubkey)
{
	bssl::UniquePtr<BIGNUM> x_bn(
		BN_bin2bn(pubkey.x, sizeof(pubkey.x), nullptr));
	if (x_bn == nullptr) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	bssl::UniquePtr<BIGNUM> y_bn(
		BN_bin2bn(pubkey.y, sizeof(pubkey.y), nullptr));
	if (y_bn == nullptr) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	static_assert(sizeof(pubkey.x) == 32);
	static_assert(sizeof(pubkey.y) == 32);
	auto key = new_p256_key();
	if (!key.has_value()) {
		return std::unexpected(key.error());
	}

	if (EC_KEY_set_public_key_affine_coordinates(key->get(), x_bn.get(),
						     y_bn.get()) != 1) {
		return std::unexpected(EC_ERROR_INVAL);
	}
	return *std::move(key);
}

std::expected<bssl::UniquePtr<EC_KEY>, ec_error_list>
create_ec_key_from_privkey(
	std::span<const uint8_t, FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN> privkey)
{
	auto key = new_p256_key();
	if (!key.has_value()) {
		return std::unexpected(key.error());
	}

	if (EC_KEY_oct2priv(key->get(), privkey.data(), privkey.size()) != 1) {
		return std::unexpected(EC_ERROR_INVAL);
	}
	return *std::move(key);
}

std::expected<CleanseWrapper<std::array<uint8_t, FP_PAIRING_KEY_LEN> >,
	      ec_error_list>
generate_ecdh_shared_secret_without_kdf(const EC_KEY &private_key,
					const EC_KEY &public_key)
{
	const EC_POINT *public_point = EC_KEY_get0_public_key(&public_key);
	if (public_point == nullptr) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	CleanseWrapper<std::array<uint8_t, FP_PAIRING_KEY_LEN> > secret{};
	if (ECDH_compute_key(secret.data(), secret.size(), public_point,
			     &private_key, nullptr) == -1) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	return secret;
}

std::expected<CleanseWrapper<std::array<uint8_t, SHA256_DIGEST_LENGTH> >,
	      ec_error_list>
generate_session_key(
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce,
	std::span<const uint8_t, FP_PAIRING_KEY_LEN> pairing_key,
	std::span<const uint8_t> session_context)
{
	std::array inputs{
		std::span<const uint8_t>{ fpmcu_nonce },
		std::span<const uint8_t>{ peer_nonce },
		session_context,
	};

	return hmac_sha256(pairing_key, inputs)
		.transform([](const auto &digest) {
			return CleanseWrapper(digest);
		});
}

std::expected<void, ec_error_list> decrypt_data_with_session_key(
	std::span<const uint8_t, SHA256_DIGEST_LENGTH> session_key,
	std::span<const uint8_t> input, std::span<uint8_t> output,
	std::span<const uint8_t, FP_AES_KEY_NONCE_BYTES> nonce,
	std::span<const uint8_t, FP_AES_KEY_TAG_BYTES> tag,
	std::string_view aad)
{
	if (input.size() != output.size()) {
		return std::unexpected(EC_ERROR_OVERFLOW);
	}

	bssl::ScopedEVP_AEAD_CTX ctx;
	if (!EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_256_gcm(),
			       session_key.data(), session_key.size(),
			       tag.size(), nullptr)) {
		return std::unexpected(EC_ERROR_INVAL);
	}

	const auto aad_bytes = as_operation_span(aad);
	if (!EVP_AEAD_CTX_open_gather(ctx.get(), output.data(), nonce.data(),
				      nonce.size(), input.data(), input.size(),
				      tag.data(), tag.size(), aad_bytes.data(),
				      aad_bytes.size())) {
		return std::unexpected(EC_ERROR_UNKNOWN);
	}

	return {};
}

std::expected<std::array<uint8_t, SHA256_DIGEST_LENGTH>, ec_error_list>
compute_message_signature(
	std::span<const uint8_t, SHA256_DIGEST_LENGTH> session_key,
	std::span<const uint8_t> context, std::string_view sender,
	std::string_view operation,
	std::span<const uint8_t, FP_CHALLENGE_SIZE> challenge)
{
	std::array inputs{
		std::span<const uint8_t>{ challenge },
		context,
		as_operation_span(sender),
		as_operation_span(operation),
	};

	return hmac_sha256(session_key, inputs);
}
