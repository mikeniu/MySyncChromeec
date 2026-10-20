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
#include "fpsensor/fpsensor_template_store.h"
#include "fpsensor_driver.h"
#include "fpsensor_image_frame_params.h"
#include "fpsensor_matcher.h"
#include "fpsensor_state_driver.h"
#include "link_defs.h"
#include "timer.h"

#include <stdbool.h>
#include <stdint.h>

#include <array>
#include <optional>
#include <span>

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

struct fpsensor_context {
	/** Index of the last enrolled but not retrieved template. */
	uint16_t template_newly_enrolled;
	/** Number of used templates */
	uint16_t templ_valid;
	/** Bitmap of the templates with local modifications */
	uint32_t templ_dirty;
	/** Status of the FP encryption engine & context. */
	uint32_t fp_encryption_status;
	atomic_t fp_events;
	uint32_t sensor_mode;
	/** Type of the image captured from the fingerprint sensor in the
	 * most recent successful capture.
	 */
	enum fp_capture_type current_capture_type;
	/** Map of the capture type to frame parameters. */
	FpImageFrameParamsCache fp_image_frame_params_cache;
	/** Part of the IKM used to derive encryption keys received from the
	 * TPM.
	 */
	std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed;
	/** Current user ID */
	std::array<uint8_t, FP_CONTEXT_USERID_BYTES> user_id;
	struct positive_match_secret_state positive_match_secret_state;
	/** Salt used in derivation of positive match secret. */
	uint8_t fp_positive_match_salt[FP_MAX_FINGER_COUNT]
				      [FP_POSITIVE_MATCH_SALT_BYTES];
	/** ID of encrypted template in 'fp_enc_buffer'.
	 *  The template is available if 'fp_encryption_status' has
	 *  FP_ENCRYPTED_TEMPLATE_READY bit set.
	 */
	uint16_t template_encrypted_id;
	/** Template decryption result. */
	atomic_t template_decryption_result;
};

extern struct fpsensor_context global_context;

/*
 * Check if FP TPM seed has been set.
 *
 * @return 1 if the seed has been set, 0 otherwise.
 */
int fp_tpm_seed_is_set(void);

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
void fp_reset_context(void);

/**
 * Clear all fingerprint templates associated with the current user id and
 * reset the sensor.
 */
void fp_reset_and_clear_context(void);

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
 * @param mode          new mode to change to
 * @param mode_output   resulting mode
 * @param mac           the MAC, required for authenticated modes
 * @return EC_RES_SUCCESS on success. Error code on failure.
 */
enum ec_status
fp_set_sensor_mode(uint32_t mode, uint32_t *mode_output,
		   std::optional<std::span<const uint8_t, FP_MAC_LENGTH> > mac);

/**
 * Read the match secret from the positive match salt.
 *
 * @param fgr the index of positive match salt.
 * @param positive_match_secret the match secret that derived from the salt.
 */
enum ec_status
fp_read_match_secret(int8_t fgr,
		     std::span<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES>
			     positive_match_secret);

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_H_ */
