/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint template storage and encryption buffer types */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_TEMPLATE_STORE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_TEMPLATE_STORE_H_

#include "atomic.h"
#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_utils.h"
#include "fpsensor_driver.h"
#include "fpsensor_matcher.h"
#include "fpsensor_state_driver.h"
#include "link_defs.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

/* if no special memory regions are defined, fallback on regular SRAM */
#ifndef FP_TEMPLATE_SECTION
#define FP_TEMPLATE_SECTION
#endif

inline constexpr size_t kFpAlgorithmEncryptedTemplateSize =
	FP_ALGORITHM_TEMPLATE_SIZE + FP_POSITIVE_MATCH_SALT_BYTES +
	sizeof(ec_fp_template_encryption_metadata);

using FpTemplate = std::array<uint8_t, FP_ALGORITHM_TEMPLATE_SIZE>;
using FpPositiveMatchSalt = std::array<uint8_t, FP_POSITIVE_MATCH_SALT_BYTES>;

/**
 * Validated fingerprint template index in [0, FP_MAX_FINGER_COUNT).
 */
class FpTemplateId {
    public:
	static constexpr std::expected<FpTemplateId, ec_status>
	create(std::integral auto idx) noexcept
	{
		if (std::cmp_less(idx, 0) ||
		    std::cmp_greater_equal(idx, FP_MAX_FINGER_COUNT)) {
			return std::unexpected(EC_RES_INVALID_PARAM);
		}
		return FpTemplateId(static_cast<uint16_t>(idx));
	}

	static constexpr std::expected<FpTemplateId, ec_status>
	create_enrolled(std::integral auto idx, uint16_t templ_valid) noexcept
	{
		const auto id = create(idx);
		if (!id.has_value()) {
			return std::unexpected(id.error());
		}
		if (id->raw() >= templ_valid) {
			return std::unexpected(EC_RES_UNAVAILABLE);
		}
		return *id;
	}

	[[nodiscard]] constexpr uint16_t raw() const noexcept
	{
		return id_;
	}

	constexpr bool
	operator==(const FpTemplateId &) const noexcept = default;

    private:
	explicit constexpr FpTemplateId(uint16_t id) noexcept : id_(id)
	{
	}

	uint16_t id_ = 0;
};

struct FpEncryptedTemplateBuffer {
	ec_fp_template_encryption_metadata metadata{};
	FpTemplate fp_template{};
	FpPositiveMatchSalt positive_match_salt{};

	[[nodiscard]] auto as_bytes(this auto &self) noexcept
	{
		using Byte = std::conditional_t<
			std::is_const_v<std::remove_reference_t<decltype(self)> >,
			const uint8_t, uint8_t>;
		return std::span<Byte, kFpAlgorithmEncryptedTemplateSize>(
			reinterpret_cast<Byte *>(&self), sizeof(self));
	}

	[[nodiscard]] auto payload(this auto &self) noexcept
	{
		return self.as_bytes()
			.template subspan<sizeof(metadata),
					  FP_ALGORITHM_TEMPLATE_SIZE +
						  FP_POSITIVE_MATCH_SALT_BYTES>();
	}

	[[nodiscard]] bool is_zeroed() const noexcept
	{
		return std::ranges::none_of(as_bytes(), std::identity{});
	}

	[[nodiscard]] constexpr bool has_valid_metadata() const noexcept
	{
		return metadata.struct_version == FP_TEMPLATE_FORMAT_VERSION;
	}

	[[nodiscard]] bool has_nontrivial_salt() const noexcept
	{
		return !bytes_are_trivial(positive_match_salt);
	}

	[[nodiscard]] constexpr std::expected<void, ec_error_list>
	validate_offset(uint32_t offset, uint32_t size) const noexcept
	{
		if (const ec_error_list err = validate_fp_buffer_offset(
			    sizeof(*this), offset, size);
		    err != EC_SUCCESS) {
			return std::unexpected(err);
		}
		return {};
	}

	[[nodiscard]] std::expected<void, ec_error_list>
	write_chunk(uint32_t offset, std::span<const uint8_t> chunk) noexcept
	{
		return checked_subspan(as_bytes(), offset, chunk.size())
			.transform([chunk](std::span<uint8_t> dest) {
				std::ranges::copy(chunk, dest.begin());
			});
	}

	[[nodiscard]] std::expected<void, ec_error_list>
	read_chunk(uint32_t offset, std::span<uint8_t> dest) const noexcept
	{
		return checked_subspan(as_bytes(), offset, dest.size())
			.transform([dest](std::span<const uint8_t> src) {
				std::ranges::copy(src, dest.begin());
			});
	}

	[[nodiscard]] std::expected<void, ec_error_list>
	seal(const FpTemplate &templ, const FpPositiveMatchSalt &salt,
	     std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	     std::span<const uint8_t, FP_CONTEXT_NONCE_BYTES> nonce,
	     std::span<const uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES>
		     encryption_salt);

	[[nodiscard]] std::expected<void, ec_status>
	open(std::span<const uint8_t> context,
	     std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);

	void clear() noexcept;
};
static_assert(sizeof(FpEncryptedTemplateBuffer) ==
	      kFpAlgorithmEncryptedTemplateSize);

/* --- Global variables defined in fpsensor_template_store.cc --- */

/* Fingers templates for the current user */
extern std::array<FpTemplate, FP_MAX_FINGER_COUNT> fp_template;
/* Encryption/decryption buffer */
/* TODO: On-the-fly encryption/decryption without a dedicated buffer */
/*
 * Store the encryption metadata at the beginning of the buffer containing the
 * ciphered data.
 */
extern FpEncryptedTemplateBuffer fp_enc_buffer;

#ifndef CONFIG_ZEPHYR
#define FP_NO_SUCH_TEMPLATE (UINT16_MAX)
struct positive_match_secret_state {
#else
struct FpPositiveMatchSecretState {
#endif
	/* b/478160541 */
	static constexpr std::chrono::seconds kValidityWindow{ 15 };

#ifdef CONFIG_ZEPHYR
	/* Index of the most recently matched template, if readable. */
	std::optional<FpTemplateId> template_matched = std::nullopt;
	/* Deadline to read positive match secret. */
	FpTimestamp deadline{};
#else
	uint16_t template_matched = FP_NO_SUCH_TEMPLATE;
	bool readable = false;
	timestamp_t deadline{};
#endif

	/**
	 * Allow reading positive match secret for |fgr| in the next 15 seconds.
	 *
	 * @param fgr the index of template to enable positive match secret.
	 * @return {} if the request is valid, error code otherwise.
	 */
	std::expected<void, ec_error_list> enable(FpTemplateId fgr);

	/**
	 * Disallow positive match secret for any finger to be read.
	 */
	void disable() noexcept
	{
		*this = {};
	}

	/**
	 * Atomically take, clear, and validate the positive match secret state
	 * so it can only be used once.
	 */
	[[nodiscard]] std::expected<FpTemplateId, ec_status>
	consume(std::optional<FpTemplateId> expected_fgr = std::nullopt,
		FpTimestamp now = FpClock::now())
	{
		const auto state = std::exchange(*this, {});
		if (!expected_fgr.has_value() && !state.has_any_match()) {
			return std::unexpected(EC_RES_ACCESS_DENIED);
		}
		if (state.is_expired(now)) {
			return std::unexpected(EC_RES_TIMEOUT);
		}
		if (expected_fgr.has_value() &&
		    !state.is_matched(*expected_fgr)) {
			return std::unexpected(EC_RES_ACCESS_DENIED);
		}
#ifdef CONFIG_ZEPHYR
		return *state.template_matched;
#else
		return *FpTemplateId::create(state.template_matched);
#endif
	}

	[[nodiscard]] constexpr bool is_matched(FpTemplateId fgr) const noexcept
	{
#ifdef CONFIG_ZEPHYR
		return template_matched == fgr;
#else
		return readable && template_matched == fgr.raw();
#endif
	}

	[[nodiscard]] constexpr bool has_any_match() const noexcept
	{
#ifdef CONFIG_ZEPHYR
		return template_matched.has_value();
#else
		return readable &&
		       FpTemplateId::create(template_matched).has_value();
#endif
	}

	[[nodiscard]] constexpr bool
	is_expired(FpTimestamp now = FpClock::now()) const noexcept
	{
#ifdef CONFIG_ZEPHYR
		return now >= deadline;
#else
		return now >= FpTimestamp(FpClock::duration(deadline.val));
#endif
	}
};

#ifndef CONFIG_ZEPHYR
using FpPositiveMatchSecretState = positive_match_secret_state;

inline enum ec_error_list
fp_enable_positive_match_secret(int8_t fgr, FpPositiveMatchSecretState *state)
{
	const auto id = FpTemplateId::create(fgr);
	if (!id.has_value()) {
		state->disable();
		return EC_ERROR_INVAL;
	}
	return state->enable(*id).error_or(EC_SUCCESS);
}

inline void
fp_disable_positive_match_secret(FpPositiveMatchSecretState *state) noexcept
{
	state->disable();
}
#endif

struct FpMatchResult {
	FpMkbpEvent event{};
	std::optional<FpTemplateId> matched_id = std::nullopt;
	bool valid = true;
};

/**
 * Domain store for fingerprint templates, salts, encryption buffer, and match
 * secret state.
 */
struct FpTemplateStore {
	/** Fingers templates for the current user. */
	std::span<FpTemplate, FP_MAX_FINGER_COUNT> templates = fp_template;
	/** Encryption/decryption buffer. */
	FpEncryptedTemplateBuffer *enc_buf = &fp_enc_buffer;
	/** Index of the last enrolled but not retrieved template. */
	std::optional<FpTemplateId> template_newly_enrolled = std::nullopt;
	/** Number of used templates. */
	uint16_t templ_valid = 0;
	/** Bitmap of the templates with local modifications. */
	std::bitset<FP_MAX_FINGER_COUNT> templ_dirty{};
	FpPositiveMatchSecretState positive_match_secret_state{};
	/** Salt used in derivation of positive match secret. */
	std::array<FpPositiveMatchSalt, FP_MAX_FINGER_COUNT>
		fp_positive_match_salt{};
	/**
	 * ID of encrypted template in 'enc_buf'.
	 * The template is available if 'crypto.encrypted_template_ready' is
	 * true.
	 */
	std::optional<FpTemplateId> template_encrypted_id = std::nullopt;
	/** Template decryption result. */
	atomic_t template_decryption_result = 0;
	/** Rate limiter for template encryption (b/114160734). */
	FpRateLimiter encryption_rate_limiter{};

	constexpr void mark_dirty(FpTemplateId id) noexcept
	{
		templ_dirty.set(id.raw());
	}

	constexpr void mark_dirty_mask(uint32_t mask) noexcept
	{
		templ_dirty |= std::bitset<FP_MAX_FINGER_COUNT>(mask);
	}

	constexpr void clear_dirty(FpTemplateId id) noexcept
	{
		templ_dirty.reset(id.raw());
	}

	void set_decryption_result(ec_status res) noexcept
	{
		atomic_set(&template_decryption_result, res);
	}

	[[nodiscard]] ec_status get_decryption_result() const noexcept
	{
		return static_cast<ec_status>(
			atomic_get(&template_decryption_result));
	}

	[[nodiscard]] constexpr std::expected<FpTemplateId, ec_status>
	next_free_id() const noexcept
	{
		return FpTemplateId::create(templ_valid)
			.transform_error(
				[](ec_status) { return EC_RES_OVERFLOW; });
	}

	[[nodiscard]] constexpr fp_template_info
	to_template_info() const noexcept
	{
		static_assert(FP_MAX_FINGER_COUNT <= 32);
		return {
			.template_size = kFpAlgorithmEncryptedTemplateSize,
			.template_max = FP_MAX_FINGER_COUNT,
			.template_valid = templ_valid,
			.template_dirty =
				static_cast<uint32_t>(templ_dirty.to_ulong()),
			.template_version = FP_TEMPLATE_FORMAT_VERSION,
		};
	}

	void clear_finger(FpTemplateId id) noexcept;
	void reset() noexcept;
	void finish_enrollment(FpTemplateId id, bool auth_enabled);
	[[nodiscard]] FpMatchResult match(std::span<uint8_t> image_buffer,
					  bool allow_template_update);
	[[nodiscard]] std::expected<void, ec_error_list>
	encrypt(FpTemplateId fgr,
		std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);
	[[nodiscard]] std::expected<void, ec_status>
	commit(std::span<const uint8_t> context,
	       std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);
	[[nodiscard]] std::expected<FpPositiveMatchSecret, ec_status>
	read_match_secret(
		int8_t fgr,
		std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
		std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed);
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_TEMPLATE_STORE_H_ */
