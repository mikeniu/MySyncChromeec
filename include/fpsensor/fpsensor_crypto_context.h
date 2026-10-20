/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint cryptographic and authenticated session context */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CRYPTO_CONTEXT_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CRYPTO_CONTEXT_H_

#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

struct FpTpmSeed {
	using Bytes = std::array<uint8_t, FP_CONTEXT_TPM_BYTES>;

	Bytes value{};
	bool set_flag = false;

	[[nodiscard]] constexpr bool is_set() const noexcept
	{
		return set_flag;
	}

	constexpr void
	set(std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> seed) noexcept
	{
		std::ranges::copy(seed, value.begin());
		set_flag = true;
	}

	void clear() noexcept
	{
		cleanse(value);
		set_flag = false;
	}

	[[nodiscard]] constexpr const Bytes &bytes() const noexcept
	{
		return value;
	}

	[[nodiscard]] std::expected<void, ec_status>
	set_from_host(const ec_params_fp_seed &params);
};

struct FpUserId {
	using Bytes = std::array<uint8_t, FP_CONTEXT_USERID_BYTES>;

	Bytes value{};

	[[nodiscard]] constexpr bool is_set() const noexcept
	{
		return std::ranges::any_of(value, std::identity{});
	}

	void
	set(std::span<const uint32_t, FP_CONTEXT_USERID_WORDS> words) noexcept
	{
		std::ranges::copy(
			std::as_bytes(words),
			std::as_writable_bytes(std::span(value)).begin());
	}

	void clear() noexcept
	{
		cleanse(value);
	}

	[[nodiscard]] constexpr const Bytes &bytes() const noexcept
	{
		return value;
	}

	/**
	 * The context for signing/verifying messages is the Android user id,
	 * which is the first 4-byte integer of the user ID.
	 */
	[[nodiscard]] constexpr std::span<const uint8_t, sizeof(uint32_t)>
	auth_context() const noexcept
	{
		return std::span(value).first<sizeof(uint32_t)>();
	}
};

struct FpSessionNonce {
	using Bytes = std::array<uint8_t, FP_CK_SESSION_NONCE_LEN>;

	CleanseWrapper<Bytes> value{};

	[[nodiscard]] static FpSessionNonce
	generate(std::span<uint8_t, FP_CK_SESSION_NONCE_LEN> out);

	[[nodiscard]] constexpr const Bytes &bytes() const noexcept
	{
		return value;
	}
};

struct FpAuthChallenge {
	static constexpr std::chrono::seconds kValidityWindow{ 5 };

	std::array<uint8_t, FP_CHALLENGE_SIZE> value{};
	FpTimestamp deadline{};

	[[nodiscard]] static FpAuthChallenge
	generate(std::span<uint8_t, FP_CHALLENGE_SIZE> out,
		 FpTimestamp now = FpClock::now());

	[[nodiscard]] static std::expected<FpAuthChallenge, ec_error_list>
	consume(std::optional<FpAuthChallenge> &active,
		FpTimestamp now = FpClock::now()) noexcept;

	[[nodiscard]] constexpr bool
	is_expired(FpTimestamp now = FpClock::now()) const noexcept
	{
		return now >= deadline;
	}
};

/**
 * Domain context for cryptographic keys, user context, encryption status, and
 * authenticated session state.
 */
struct FpCryptoContext {
	/** Whether an authenticated session is established. */
	bool session_established = false;
	/** Whether an encrypted template is available in |fp_enc_buffer|. */
	bool encrypted_template_ready = false;
	/**
	 * Part of the IKM used to derive encryption keys received from the
	 * TPM.
	 */
	FpTpmSeed tpm_seed{};
	/** Current user ID. */
	FpUserId user_id{};
	/** The session nonce for session key, if generated. */
	std::optional<FpSessionNonce> session_nonce = std::nullopt;
	/** Current session key. */
	FpSessionKey session_key{};
	/** The FPMCU pairing key. */
	FpPairingKey pairing_key{};
	/** Ephemeral FPMCU ECDH private key during pairing key establishment.
	 */
	std::optional<FpP256Key> ecdh_key = std::nullopt;
	/** Active single-use authentication challenge, if any. */
	std::optional<FpAuthChallenge> active_challenge = std::nullopt;

	[[nodiscard]] constexpr bool auth_enabled() const noexcept
	{
		return session_established;
	}

	constexpr void set_auth_enabled(bool enabled) noexcept
	{
		session_established = enabled;
	}

	[[nodiscard]] constexpr uint32_t encryption_status() const noexcept
	{
		return (tpm_seed.is_set() ? FP_ENC_STATUS_SEED_SET : 0) |
		       (session_established ?
				FP_CONTEXT_STATUS_SESSION_ESTABLISHED :
				0) |
		       (session_nonce.has_value() ?
				FP_CONTEXT_SESSION_NONCE_SET :
				0) |
		       (user_id.is_set() ? FP_CONTEXT_USER_ID_SET : 0) |
		       (active_challenge.has_value() ? FP_AUTH_CHALLENGE_SET :
						       0) |
		       (encrypted_template_ready ? FP_ENCRYPTED_TEMPLATE_READY :
						   0);
	}

	void reset_user_context() noexcept;
	void reset_session() noexcept;
	void generate_nonce(std::span<uint8_t, FP_CK_SESSION_NONCE_LEN> out);
	[[nodiscard]] std::expected<void, ec_status>
	generate_challenge(std::span<uint8_t, FP_CHALLENGE_SIZE> out);
	[[nodiscard]] std::expected<FpAuthChallenge, ec_error_list>
	consume_challenge(FpTimestamp now = FpClock::now()) noexcept;
	[[nodiscard]] std::expected<FpSessionKey, ec_error_list>
	derive_session_key(std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN>
				   peer_nonce) const;
	[[nodiscard]] std::expected<void, ec_error_list>
	validate_request(std::span<const uint8_t> context,
			 std::string_view operation,
			 std::span<const uint8_t, FP_MAC_LENGTH> mac);
	[[nodiscard]] std::expected<std::array<uint8_t, FP_MAC_LENGTH>,
				    ec_error_list>
	sign_message(std::span<const uint8_t> context,
		     std::string_view operation,
		     std::span<const uint8_t, FP_CHALLENGE_SIZE> peer_challenge)
		const;
	[[nodiscard]] std::expected<fp_elliptic_curve_public_key, ec_status>
	establish_pairing_key_keygen();
	[[nodiscard]] std::expected<ec_fp_encrypted_pairing_key, ec_status>
	establish_pairing_key_wrap(
		const fp_elliptic_curve_public_key &peers_pubkey);
	[[nodiscard]] std::expected<void, ec_status> load_pairing_key(
		const ec_fp_encrypted_pairing_key &encrypted_pairing_key);
	[[nodiscard]] std::expected<void, ec_status> establish_ascp_pairing_key(
		std::span<const uint8_t, FP_ASCP_KEY_SIZE> pk_g);
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CRYPTO_CONTEXT_H_ */
