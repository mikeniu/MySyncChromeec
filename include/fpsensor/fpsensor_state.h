/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_

#include "atomic.h"
#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_utils.h"
#include "fpsensor_driver.h"
#include "fpsensor_image_frame_params.h"
#include "fpsensor_matcher.h"
#include "fpsensor_state_driver.h"
#include "link_defs.h"
#include "timer.h"

#include <stdbool.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <expected>
#include <optional>
#include <span>
#include <utility>

#ifdef CONFIG_ZEPHYR
#include <zephyr/linker/devicetree_regions.h>

#if DT_NODE_EXISTS(DT_CHOSEN(cros_fp_fingerprint_memory_frame))
#define FP_FRAME_SECTION                                       \
	__attribute__((__section__(LINKER_DT_NODE_REGION_NAME( \
		DT_CHOSEN(cros_fp_fingerprint_memory_frame)))))
#endif

#if DT_NODE_EXISTS(DT_CHOSEN(cros_fp_fingerprint_memory_template))
#define FP_TEMPLATE_SECTION                                    \
	__attribute__((__section__(LINKER_DT_NODE_REGION_NAME( \
		DT_CHOSEN(cros_fp_fingerprint_memory_template)))))
#endif
#endif /* CONFIG_ZEPHYR */

/* if no special memory regions are defined, fallback on regular SRAM */
#ifndef FP_FRAME_SECTION
#define FP_FRAME_SECTION
#endif
#ifndef FP_TEMPLATE_SECTION
#define FP_TEMPLATE_SECTION
#endif

#define FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE                         \
	(FP_ALGORITHM_TEMPLATE_SIZE + FP_POSITIVE_MATCH_SALT_BYTES + \
	 sizeof(struct ec_fp_template_encryption_metadata))

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
		auto id = create(idx);
		if (!id.has_value()) {
			return id;
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

	[[nodiscard]] constexpr uint32_t bit() const noexcept
	{
		static_assert(FP_MAX_FINGER_COUNT < 32);
		return BIT(id_);
	}

	constexpr bool
	operator==(const FpTemplateId &) const noexcept = default;

    private:
	explicit constexpr FpTemplateId(uint16_t id) noexcept : id_(id)
	{
	}

	uint16_t id_ = 0;
};

void cleanse(std::span<uint8_t> bytes) noexcept;

struct enc_buffer {
	struct ec_fp_template_encryption_metadata metadata{};
	FpTemplate fp_template{};
	FpPositiveMatchSalt positive_match_salt{};

	[[nodiscard]] std::span<uint8_t, FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE>
	as_bytes() noexcept
	{
		return std::span<uint8_t, FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE>(
			reinterpret_cast<uint8_t *>(this), sizeof(*this));
	}

	[[nodiscard]] std::span<uint8_t, FP_ALGORITHM_TEMPLATE_SIZE +
						 FP_POSITIVE_MATCH_SALT_BYTES>
	payload() noexcept
	{
		return std::span<uint8_t, FP_ALGORITHM_TEMPLATE_SIZE +
						  FP_POSITIVE_MATCH_SALT_BYTES>(
			fp_template.data(),
			fp_template.size() + positive_match_salt.size());
	}

	[[nodiscard]] constexpr bool has_valid_metadata() const noexcept
	{
		return metadata.struct_version == FP_TEMPLATE_FORMAT_VERSION;
	}

	[[nodiscard]] bool has_nontrivial_salt() const noexcept
	{
		return !bytes_are_trivial(positive_match_salt);
	}

	[[nodiscard]] std::expected<void, ec_error_list>
	write_chunk(uint32_t offset, std::span<const uint8_t> chunk) noexcept
	{
		return checked_subspan(as_bytes(), offset, chunk.size())
			.transform([chunk](std::span<uint8_t> dest) {
				std::ranges::copy(chunk, dest.begin());
			});
	}

	void clear() noexcept;
};
BUILD_ASSERT(sizeof(enc_buffer) == FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE);

#define FP_NO_SUCH_TEMPLATE (UINT16_MAX)

/* --- Global variables defined in fpsensor_state.c --- */

/* Fingers templates for the current user */
extern std::array<FpTemplate, FP_MAX_FINGER_COUNT> fp_template;
/* Encryption/decryption buffer */
/* TODO: On-the-fly encryption/decryption without a dedicated buffer */
/*
 * Store the encryption metadata at the beginning of the buffer containing the
 * ciphered data.
 */
extern struct enc_buffer fp_enc_buffer;

struct positive_match_secret_state {
	/* Index of the most recently matched template, if readable. */
	std::optional<FpTemplateId> template_matched = std::nullopt;
	/* Deadline to read positive match secret. */
	timestamp_t deadline{};

	/**
	 * Allow reading positive match secret for |fgr| in the next 15 seconds.
	 *
	 * @param fgr the index of template to enable positive match secret.
	 * @return EC_SUCCESS if the request is valid, error code otherwise.
	 */
	ec_error_list enable(FpTemplateId fgr);

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
		timestamp_t now = get_time())
	{
		const positive_match_secret_state state =
			std::exchange(*this, {});
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
		return *state.template_matched;
	}

	[[nodiscard]] constexpr bool is_matched(FpTemplateId fgr) const noexcept
	{
		return template_matched == fgr;
	}

	[[nodiscard]] constexpr bool has_any_match() const noexcept
	{
		return template_matched.has_value();
	}

	[[nodiscard]] bool is_expired(timestamp_t now = get_time()) const
	{
		return timestamp_expired(deadline, &now);
	}
};

struct fpsensor_context {
	/** Index of the last enrolled but not retrieved template. */
	std::optional<FpTemplateId> template_newly_enrolled = std::nullopt;
	/** Number of used templates */
	uint16_t templ_valid = 0;
	/** Bitmap of the templates with local modifications */
	uint32_t templ_dirty = 0;
	/** Status of the FP encryption engine & context. */
	uint32_t fp_encryption_status = 0;
	atomic_t fp_events = 0;
	uint32_t sensor_mode = 0;
	/** Type of the image captured from the fingerprint sensor in the
	 * most recent successful capture.
	 */
	std::optional<FpCaptureType> current_capture_type = std::nullopt;
	/** Map of the capture type to frame parameters. */
	FpImageFrameParamsCache fp_image_frame_params_cache{};
	/** Part of the IKM used to derive encryption keys received from the
	 * TPM.
	 */
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed{};
	/** Current user ID */
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> user_id{};
	struct positive_match_secret_state positive_match_secret_state{};
	/** Salt used in derivation of positive match secret. */
	std::array<FpPositiveMatchSalt, FP_MAX_FINGER_COUNT>
		fp_positive_match_salt{};
	/** ID of encrypted template in 'fp_enc_buffer'.
	 *  The template is available if 'fp_encryption_status' has
	 *  FP_ENCRYPTED_TEMPLATE_READY bit set.
	 */
	std::optional<FpTemplateId> template_encrypted_id = std::nullopt;
	/** Template decryption result. */
	atomic_t template_decryption_result = 0;
	/** Rate-limiting deadline for template encryption (b/114160734). */
	timestamp_t encryption_deadline{};

	[[nodiscard]] constexpr bool has_any_mode(uint32_t mask) const noexcept
	{
		return (sensor_mode & mask) != 0;
	}

	[[nodiscard]] constexpr bool is_crypto_in_progress() const noexcept
	{
		return has_any_mode(FP_MODES_CRYPTO_IN_PROGRESS);
	}

	constexpr void mark_template_dirty(FpTemplateId id) noexcept
	{
		templ_dirty |= id.bit();
	}

	constexpr void clear_template_dirty(FpTemplateId id) noexcept
	{
		templ_dirty &= ~id.bit();
	}

	constexpr void set_user_id(
		std::span<const uint8_t, FP_CONTEXT_USERID_BYTES> new_user_id)
		noexcept
	{
		std::ranges::copy(new_user_id, user_id.begin());
		if (std::ranges::any_of(user_id,
					[](uint8_t b) { return b != 0; })) {
			fp_encryption_status |= FP_CONTEXT_USER_ID_SET;
		}
	}

	[[nodiscard]] constexpr bool tpm_seed_set() const noexcept
	{
		return (fp_encryption_status & FP_ENC_STATUS_SEED_SET) != 0;
	}

	constexpr void set_tpm_seed(
		std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> seed) noexcept
	{
		std::ranges::copy(seed, tpm_seed.begin());
		fp_encryption_status |= FP_ENC_STATUS_SEED_SET;
	}

	void clear_tpm_seed() noexcept
	{
		cleanse(tpm_seed);
		fp_encryption_status &= ~FP_ENC_STATUS_SEED_SET;
	}

	[[nodiscard]] constexpr bool encrypted_template_ready() const noexcept
	{
		return (fp_encryption_status & FP_ENCRYPTED_TEMPLATE_READY) !=
		       0;
	}

	constexpr void request_template_encryption(FpTemplateId id) noexcept
	{
		fp_encryption_status &= ~FP_ENCRYPTED_TEMPLATE_READY;
		template_encrypted_id = id;
	}

	constexpr void set_encrypted_template_ready(bool ready) noexcept
	{
		if (ready) {
			fp_encryption_status |= FP_ENCRYPTED_TEMPLATE_READY;
		} else {
			fp_encryption_status &= ~FP_ENCRYPTED_TEMPLATE_READY;
		}
	}

	constexpr void clear_encrypted_template() noexcept
	{
		fp_encryption_status &= ~FP_ENCRYPTED_TEMPLATE_READY;
		template_encrypted_id.reset();
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

	void add_events(uint32_t evt) noexcept
	{
		atomic_or(&fp_events, evt);
	}

	[[nodiscard]] uint32_t take_events() noexcept
	{
		return atomic_clear(&fp_events);
	}

	[[nodiscard]] constexpr std::expected<FpTemplateId, ec_status>
	next_free_template_id() const noexcept
	{
		return FpTemplateId::create(templ_valid)
			.transform_error(
				[](ec_status) { return EC_RES_OVERFLOW; });
	}

	/**
	 * Check whether template encryption is currently rate-limited (at most
	 * 1 encrypted message per second, see b/114160734), and if not, advance
	 * the encryption deadline by 1 second.
	 */
	[[nodiscard]] std::expected<void, ec_host_cmd_status>
	try_acquire_encryption_slot(timestamp_t now = get_time())
	{
		if (!timestamp_expired(encryption_deadline, &now)) {
			return std::unexpected(EC_HOST_CMD_BUSY);
		}
		encryption_deadline.val = now.val + (1 * SECOND);
		return {};
	}

	/**
	 * Return the active captured-frame slice of |buffer| for the most
	 * recent capture, or EC_RES_INVALID_PARAM if no valid frame is
	 * available.
	 */
	[[nodiscard]] std::expected<std::span<const uint8_t>, ec_status>
	captured_frame(
		std::span<const uint8_t, FP_SENSOR_IMAGE_SIZE> buffer) const
	{
		if (!current_capture_type.has_value()) {
			return std::unexpected(EC_RES_INVALID_PARAM);
		}
		const uint32_t image_offset =
			skip_image_offset(*current_capture_type) ?
				FP_SENSOR_IMAGE_OFFSET :
				0;
		return fp_image_frame_params_cache
			.get_frame_size(*current_capture_type)
			.and_then([buffer, image_offset](uint32_t frame_size) {
				return checked_subspan(buffer, image_offset,
						       frame_size);
			})
			.transform_error([](ec_error_list) {
				return EC_RES_INVALID_PARAM;
			});
	}

	[[nodiscard]] constexpr bool is_cleared() const noexcept
	{
		return std::ranges::all_of(user_id,
					   [](uint8_t b) { return b == 0; }) &&
		       templ_valid == 0 && templ_dirty == 0 &&
		       !positive_match_secret_state.has_any_match() &&
		       !(fp_encryption_status & FP_CONTEXT_USER_ID_SET);
	}

	/**
	 * The context for signing/verifying messages is the Android user id,
	 * which is the first 4-byte integer of user_id.
	 */
	[[nodiscard]] std::span<const uint8_t, sizeof(uint32_t)>
	auth_user_id_context() const noexcept
	{
		static_assert(sizeof(user_id) >= sizeof(uint32_t));
		return std::span<const uint8_t, sizeof(uint32_t)>(
			user_id.data(), sizeof(uint32_t));
	}
};

extern struct fpsensor_context global_context;

/* Simulation for unit tests. */
#ifdef __cplusplus
extern "C" {
#endif
__test_only void fp_task_simulate(void);
#ifdef __cplusplus
}
#endif

/*
 * Clear one fingerprint template.
 *
 * @param id the validated index of the template to clear.
 */
void fp_clear_finger_context(FpTemplateId id);

/**
 * Reset the current user id associated.
 */
void fp_reset_context();

/**
 * Clear all fingerprint templates associated with the current user id and
 * reset the sensor.
 */
void fp_reset_and_clear_context();

/*
 * Get the next FP event.
 *
 * @param out the pointer to the output event.
 */
#ifdef __cplusplus
extern "C" {
#endif
int fp_get_next_event(uint8_t *out);
#ifdef __cplusplus
}
#endif

/**
 * Change the sensor mode.
 *
 * @param mode  new mode to change to
 * @param mac   the MAC, required for authenticated modes
 * @return resulting FpSensorMode on success, or ec_status error code on
 *         failure.
 */
std::expected<FpSensorMode, ec_status>
fp_set_sensor_mode(uint32_t mode,
		   std::optional<std::span<const uint8_t, FP_MAC_LENGTH> > mac =
			   std::nullopt);

/**
 * Read the match secret from the positive match salt.
 *
 * @param fgr the index of positive match salt.
 * @return the match secret derived from the salt on success, or ec_status on
 *         failure.
 */
std::expected<
	CleanseWrapper<std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES> >,
	ec_status>
fp_read_match_secret(int8_t fgr);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_ */
