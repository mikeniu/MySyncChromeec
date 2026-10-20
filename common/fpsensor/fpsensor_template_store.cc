/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_template_store.h"
#include "fpsensor/fpsensor_utils.h"
#include "fpsensor_driver.h"
#include "fpsensor_matcher.h"
#include "link_defs.h"
#include "openssl/mem.h"
#include "scoped_fast_cpu.h"
#include "trng.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <expected>
#include <optional>
#include <span>
#include <utility>

#pragma GCC poison global_context

#if !defined(CONFIG_RNG)
#error "fpsensor requires RNG"
#endif

#if defined(CONFIG_CROS_EC_RO)
#error "fpsensor code should not be in RO image."
#endif

static_assert(sizeof(ec_fp_template_encryption_metadata) % 4 == 0);

/* Fingers templates for the current user */
test_mockable std::array<FpTemplate, FP_MAX_FINGER_COUNT>
	fp_template FP_TEMPLATE_SECTION __aligned(4);
static_assert(
	sizeof(fp_template[0]) % 4 == 0,
	"The size of each template must be a multiple of 4 to ensure that the next "
	"template will still be aligned by 4.");

/* Encryption/decryption buffer */
/* TODO: On-the-fly encryption/decryption without a dedicated buffer */
/*
 * Store the encryption metadata at the beginning of the buffer containing the
 * ciphered data.
 */
FpEncryptedTemplateBuffer fp_enc_buffer FP_TEMPLATE_SECTION;

namespace
{

static_assert(FP_CONTEXT_NONCE_BYTES == 12);

class ScopedTrng {
    public:
	ScopedTrng()
	{
		trng_init();
	}

	~ScopedTrng()
	{
		trng_exit();
	}

	ScopedTrng(const ScopedTrng &) = delete;
	ScopedTrng &operator=(const ScopedTrng &) = delete;

	template <typename... Spans> void fill_bytes(Spans &&...spans) const
	{
		(
			[](std::span<uint8_t> s) {
				trng_rand_bytes(s.data(), s.size());
			}(spans),
			...);
	}
};

template <typename... Spans> void fill_random_bytes(Spans &&...spans)
{
	ScopedTrng trng;
	trng.fill_bytes(std::forward<Spans>(spans)...);
}

} /* namespace */

void cleanse(std::span<uint8_t> bytes) noexcept
{
	OPENSSL_cleanse(bytes.data(), bytes.size_bytes());
}

void FpEncryptedTemplateBuffer::clear() noexcept
{
	cleanse(as_bytes());
}

std::expected<void, ec_error_list> FpEncryptedTemplateBuffer::seal(
	const FpTemplate &templ, const FpPositiveMatchSalt &salt,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	std::span<const uint8_t, FP_CONTEXT_NONCE_BYTES> nonce,
	std::span<const uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES>
		encryption_salt)
{
	const auto key =
		FpEncryptionKey::derive(encryption_salt, user_id, tpm_seed);
	if (!key.has_value()) {
		return std::unexpected(EC_ERROR_UNAVAILABLE);
	}

	fp_template = templ;
	positive_match_salt = salt;

	CleanseWrapper<std::array<uint8_t, FP_CONTEXT_TAG_BYTES> > tag{};
	std::span data = payload();
	if (!key->encrypt(data, data, nonce, tag).has_value()) {
		clear();
		return std::unexpected(EC_ERROR_UNAVAILABLE);
	}

	metadata.struct_version = FP_TEMPLATE_FORMAT_VERSION;
	std::ranges::copy(nonce, metadata.nonce);
	std::ranges::copy(encryption_salt, metadata.encryption_salt);
	std::ranges::copy(tag, metadata.tag);
	return {};
}

std::expected<void, ec_status> FpEncryptedTemplateBuffer::open(
	std::span<const uint8_t> context,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	if (!has_valid_metadata()) {
		CPRINTS("Invalid template format %d", metadata.struct_version);
		return std::unexpected(EC_RES_INVALID_PARAM);
	}

	const auto key = FpEncryptionKey::derive(metadata.encryption_salt,
						 context, tpm_seed);
	if (!key.has_value()) {
		clear();
		return std::unexpected(EC_RES_UNAVAILABLE);
	}

	std::span data = payload();
	if (!key->decrypt(data, data, metadata.nonce, metadata.tag).has_value()) {
		clear();
		return std::unexpected(EC_RES_UNAVAILABLE);
	}

	if (!has_nontrivial_salt()) {
		clear();
		return std::unexpected(EC_RES_INVALID_PARAM);
	}

	return {};
}

std::expected<void, ec_error_list>
FpPositiveMatchSecretState::enable(FpTemplateId fgr)
{
	if (has_any_match()) {
		CPRINTS("Error: positive match secret already readable.");
		disable();
		return std::unexpected(EC_ERROR_UNKNOWN);
	}

	template_matched = fgr.raw();
	readable = true;
	deadline.val = static_cast<uint64_t>(
		(FpClock::now() + kValidityWindow).time_since_epoch().count());
	return {};
}

void FpTemplateStore::clear_finger(FpTemplateId id) noexcept
{
	cleanse(templates[id.raw()]);
	cleanse(fp_positive_match_salt[id.raw()]);
}

void FpTemplateStore::reset() noexcept
{
	templ_valid = 0;
	templ_dirty.reset();
	template_newly_enrolled.reset();
	template_encrypted_id.reset();
	enc_buf->clear();
	positive_match_secret_state.disable();
	std::ranges::for_each(templates, [](auto &t) { cleanse(t); });
	std::ranges::for_each(fp_positive_match_salt,
			      [](auto &s) { cleanse(s); });
}

#ifdef HAVE_FP_PRIVATE_DRIVER
void FpTemplateStore::finish_enrollment(FpTemplateId id, bool auth_enabled)
{
	template_newly_enrolled = id;
	positive_match_secret_state.enable(id);

	/*
	 * In the classic flow we immediately use this template
	 * for matching (templ_valid is incremented).
	 *
	 * In the Fingerprint Auth flow we must reliably inform
	 * Trusted Application that the template was enrolled.
	 * To achieve this, we will not use this template for
	 * matching, until Trusted Application sends us a signed
	 * confirmation.
	 */
	if (!auth_enabled) {
		templ_valid++;
	}
}

FpMatchResult FpTemplateStore::match(std::span<uint8_t> image_buffer,
				     bool allow_template_update)
{
	/* match finger against current templates */
	positive_match_secret_state.disable();

	CPRINTS("Matching/%d ...", templ_valid);
	if (templ_valid == 0) {
		CPRINTS("No enrolled templates");
		return {
			.event = FpMkbpEvent::match(
				EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES),
			.matched_id = std::nullopt,
			.valid = false,
		};
	}

	uint32_t updated = 0;
	int32_t fgr = kFpNoSuchTemplate;
	int res = fp_finger_match(templates[0].data(), templ_valid,
				  image_buffer.data(), allow_template_update,
				  &fgr, &updated);
	CPRINTS("Match =>%d (finger %d)", res, fgr);

	std::optional<FpTemplateId> matched_id = std::nullopt;
	bool valid = true;

	if (fp_match_success(res)) {
		/*
		 * Match succeded! Let's check if template number is valid. If
		 * it is not valid, overwrite result with
		 * EC_MKBP_FP_ERR_MATCH_NO_INTERNAL.
		 */
		if (const auto match_id = FpTemplateId::create(fgr);
		    match_id.has_value()) {
			matched_id = *match_id;
			positive_match_secret_state.enable(*match_id);
		} else {
			res = EC_MKBP_FP_ERR_MATCH_NO_INTERNAL;
			valid = false;
		}
	} else if (res < 0) {
		/*
		 * Negative result means that there is a problem with code
		 * responsible for matching. Overwrite it with
		 * MATCH_NO_INTERNAL to let upper layers know what happened.
		 */
		res = EC_MKBP_FP_ERR_MATCH_NO_INTERNAL;
		valid = false;
	}

	if (res == EC_MKBP_FP_ERR_MATCH_YES_UPDATED) {
		mark_dirty_mask(updated);
	}

	return {
		.event = FpMkbpEvent::match(res, fgr),
		.matched_id = matched_id,
		.valid = valid,
	};
}
#endif /* HAVE_FP_PRIVATE_DRIVER */

std::expected<void, ec_error_list> FpTemplateStore::encrypt(
	FpTemplateId fgr,
	std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	*enc_buf = {};

	if (fgr.raw() >= templ_valid) {
		return std::unexpected(EC_ERROR_UNAVAILABLE);
	}

	CleanseWrapper<std::array<uint8_t, FP_CONTEXT_NONCE_BYTES> > nonce{};
	CleanseWrapper<std::array<uint8_t, FP_CONTEXT_ENCRYPTION_SALT_BYTES> >
		encryption_salt{};

	fill_random_bytes(nonce, encryption_salt);

	if (fgr == template_newly_enrolled) {
		/*
		 * Newly enrolled templates need new positive match
		 * salt, new positive match secret and new validation
		 * value.
		 */
		template_newly_enrolled.reset();
		fill_random_bytes(fp_positive_match_salt[fgr.raw()]);
	}

	if (const auto res = enc_buf->seal(
		    templates[fgr.raw()], fp_positive_match_salt[fgr.raw()],
		    user_id, tpm_seed, nonce, encryption_salt);
	    !res.has_value()) {
		CPRINTS("fgr%d: Failed to encrypt template", fgr.raw());
		return std::unexpected(res.error());
	}

	clear_dirty(fgr);
	return {};
}

std::expected<void, ec_status>
FpTemplateStore::commit(std::span<const uint8_t> context,
			std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	ScopedFastCpu fast_cpu;

	const auto id = next_free_id();
	if (!id.has_value()) {
		return std::unexpected(id.error());
	}

	/*
	 * The complete encrypted template has been received, start
	 * decryption.
	 */
	clear_finger(*id);
	if (const auto res = enc_buf->open(context, tpm_seed);
	    !res.has_value()) {
		CPRINTS("fgr%d: Failed to commit template (%d)", id->raw(),
			res.error());
		return std::unexpected(res.error());
	}

	templates[id->raw()] = enc_buf->fp_template;
	fp_positive_match_salt[id->raw()] = enc_buf->positive_match_salt;
	templ_valid++;
	enc_buf->clear();
	return {};
}

std::expected<FpPositiveMatchSecret, ec_status>
FpTemplateStore::read_match_secret(
	int8_t fgr, std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> user_id,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed)
{
	const auto id = FpTemplateId::create(fgr);
	if (!id.has_value()) {
		positive_match_secret_state.disable();
		CPRINTS("Invalid finger number %d", fgr);
		return std::unexpected(id.error());
	}

	const auto matched = positive_match_secret_state.consume(*id);
	if (!matched.has_value()) {
		if (matched.error() == EC_RES_TIMEOUT) {
			CPRINTS("Reading positive match secret disallowed: "
				"deadline has passed.");
		} else {
			CPRINTS("Positive match secret for finger %d is not meant to "
				"be read now.",
				fgr);
		}
		return std::unexpected(matched.error());
	}

	const auto secret = FpPositiveMatchSecret::derive(
		fp_positive_match_salt[matched->raw()], user_id, tpm_seed);
	if (!secret.has_value()) {
		CPRINTS("Failed to derive positive match secret for finger %d",
			fgr);
		/* Keep the template and encryption salt. */
		return std::unexpected(EC_RES_ERROR);
	}

	CPRINTS("Derived positive match secret for finger %d", fgr);
	return *secret;
}
