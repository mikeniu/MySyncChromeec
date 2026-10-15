/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_

#include "atomic.h"
#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor_driver.h"
#include "fpsensor_image_frame_params.h"
#include "fpsensor_matcher.h"
#include "fpsensor_state_driver.h"
#include "link_defs.h"
#include "timer.h"

#include <stdbool.h>
#include <stdint.h>

#include <array>
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

struct enc_buffer {
	struct ec_fp_template_encryption_metadata metadata{};
	std::array<uint8_t, FP_ALGORITHM_TEMPLATE_SIZE> fp_template{};
	std::array<uint8_t, FP_POSITIVE_MATCH_SALT_BYTES> positive_match_salt{};

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

	void clear() noexcept;
};
BUILD_ASSERT(sizeof(enc_buffer) == FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE);

#define FP_NO_SUCH_TEMPLATE (UINT16_MAX)

/* --- Global variables defined in fpsensor_state.c --- */

/* Fingers templates for the current user */
extern uint8_t fp_template[FP_MAX_FINGER_COUNT][FP_ALGORITHM_TEMPLATE_SIZE];
/* Encryption/decryption buffer */
/* TODO: On-the-fly encryption/decryption without a dedicated buffer */
/*
 * Store the encryption metadata at the beginning of the buffer containing the
 * ciphered data.
 */
extern struct enc_buffer fp_enc_buffer;

struct positive_match_secret_state {
	/* Index of the most recently matched template. */
	uint16_t template_matched = FP_NO_SUCH_TEMPLATE;
	/* Flag indicating positive match secret can be read. */
	bool readable = false;
	/* Deadline to read positive match secret. */
	timestamp_t deadline{};

	/**
	 * Allow reading positive match secret for |fgr| in the next 15 seconds.
	 *
	 * @param fgr the index of template to enable positive match secret.
	 * @return EC_SUCCESS if the request is valid, error code otherwise.
	 */
	ec_error_list enable(uint16_t fgr);

	/**
	 * Disallow positive match secret for any finger to be read.
	 */
	void disable() noexcept
	{
		*this = {};
	}

	/**
	 * Atomically take and clear the positive match secret state so it can
	 * only be used once.
	 */
	[[nodiscard]] positive_match_secret_state consume() noexcept
	{
		return std::exchange(*this, {});
	}

	[[nodiscard]] constexpr bool is_matched(uint16_t fgr) const noexcept
	{
		return readable && template_matched == fgr;
	}

	[[nodiscard]] constexpr bool has_any_match() const noexcept
	{
		return readable && template_matched != FP_NO_SUCH_TEMPLATE;
	}

	[[nodiscard]] bool is_expired(timestamp_t now = get_time()) const
	{
		return timestamp_expired(deadline, &now);
	}
};

struct fpsensor_context {
	/** Index of the last enrolled but not retrieved template. */
	uint16_t template_newly_enrolled = FP_NO_SUCH_TEMPLATE;
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
	fp_capture_type current_capture_type = FP_CAPTURE_TYPE_INVALID;
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
	uint8_t fp_positive_match_salt[FP_MAX_FINGER_COUNT]
				      [FP_POSITIVE_MATCH_SALT_BYTES]{};
	/** ID of encrypted template in 'fp_enc_buffer'.
	 *  The template is available if 'fp_encryption_status' has
	 *  FP_ENCRYPTED_TEMPLATE_READY bit set.
	 */
	uint16_t template_encrypted_id = FP_NO_SUCH_TEMPLATE;
	/** Template decryption result. */
	atomic_t template_decryption_result = 0;

	[[nodiscard]] constexpr bool has_any_mode(uint32_t mask) const noexcept
	{
		return (sensor_mode & mask) != 0;
	}

	[[nodiscard]] constexpr bool is_crypto_in_progress() const noexcept
	{
		return has_any_mode(FP_MODES_CRYPTO_IN_PROGRESS);
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
 * @param idx the index of the template to clear.
 */
void fp_clear_finger_context(uint16_t idx);

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
 * @param positive_match_secret the match secret that derived from the salt.
 */
ec_status
fp_read_match_secret(int8_t fgr,
		     std::span<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES>
			     positive_match_secret);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_ */
