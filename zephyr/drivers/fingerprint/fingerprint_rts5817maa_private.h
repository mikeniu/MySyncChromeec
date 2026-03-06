/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Private sensor interface */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_RTS5817MAA_PRIVATE_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_RTS5817MAA_PRIVATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Error codes from Realtek RTS5817MAA sensor library  */
typedef enum {
	RTS_FP_SUCCESS = 0,
	RTS_FP_TOO_HIGH = 1,
	RTS_FP_TOO_LOW = 2,
	RTS_FP_TOO_LEFT = 3,
	RTS_FP_TOO_RIGHT = 4,
	RTS_FP_IMMOBILE = 5,
	RTS_FP_POOR_QUALITY = 6,
	RTS_FP_MATCH_FAIL = 7,
	RTS_FP_MATCH_UPDATED = 8,
	RTS_FP_INTERNAL_ERROR = 9,
	RTS_ERROR_IMAGE_PARTIAL = -1,
	RTS_ERROR_FAST_FINGER_UP = -2,
	/* Errors that need to stop the current operation. */
	RTS_ERROR_IO_EXCEPTION = -5,
	RTS_ERROR_NOT_SUPPORT_SENSOR = -6,
	RTS_ERROR_TIMEOUT = -7,
	RTS_ERROR_NOT_ENOUGH_MEMORY = -12,
	RTS_ERROR_INVALID_PARAMETER = -22,
	RTS_ERROR_UNKNOWN = -23,
} rts_fp_return_t;

/** Capture types from RTS5817MAA sensor library */
typedef enum {
	RTS_CAPTURE_TYPE_INVALID = -1,
	RTS_CAPTURE_NORMAL_FORMAT = 0,
	RTS_CAPTURE_TYPE_MAX,
} rts_capture_mode_t;

/** Detect mode from RTS5817MAA sensor library */
typedef enum {
	RTS_DETECT_MODE_NORMAL = 0,
	RTS_DETECT_MODE_QUICK = 1,
	RTS_DETECT_MODE_MAX,
} rts_detect_mode_t;

/** Finger status from RTS5817MAA sensor library */
typedef enum {
	RTS_FINGER_NONE, 	/* finger is not present */
	RTS_FINGER_PRESENT, /* finger is present */
} rts_finger_sts_t;

/** Struct with biometric algorithm information. */
typedef struct {
	uint32_t template_size;
} rts5817maa_bio_info_t;

/**
 * Initialize rts5817maa sensor.
 *
 * @return RTS_FP_SUCCESS on success
 * @return RTS_ERROR_NOT_SUPPORT_SENSOR on incorrect sensor
 * @return RTS_ERROR_IO_EXCEPTION on SPI transfer fail.
 */
rts_fp_return_t rts5817maa_sensor_init(void);

/**
 * Get rts5817maa sensor hardware id.
 *
 * @param id return the hardware id of the sensor(rts5817maa_sensor_init must be
 * called before)
 *
 * @return RTS_FP_SUCCESS on success.
 * @return RTS_ERROR_NOT_SUPPORT_SENSOR on error sensor.
 */
rts_fp_return_t rts5817maa_sensor_get_hwid(uint8_t *id);

/**
 * Power down the sensor.
 *
 * @return RTS_FP_SUCCESS on success.
 * @return RTS_ERROR_TIMEOUT on operation timeout event occurred.
 * @return RTS_ERROR_IO_EXCEPTION on SPI transfer fail.
 */
rts_fp_return_t rts5817maa_sensor_power_down(void);

/**
 * Acquires a fingerprint image with purpose.
 *
 * @param image_data Image from sensor. Buffer must be equal to or larger than
 * sensor size.
 * @param purpose  enum on rts_capture_mode_t.
 *
 * @return RTS_FP_SUCCESS on success.
 * @return RTS_ERROR_FAST_FINGER_UP on finger removed before image was captured.
 * @return RTS_ERROR_IMAGE_PARTIAL on sensor not fully covered.
 * @return RTS_ERROR_INVALID_PARAMETER on invalid image parameter.
 * @return RTS_ERROR_TIMEOUT on operation timeout event occurred.
 * @return RTS_ERROR_IO_EXCEPTION on SPI transfer fail.
 */
rts_fp_return_t rts5817maa_sensor_acquire_image(uint8_t *image_data, rts_capture_mode_t purpose);

/**
 * Set sensor into finger detection mode.
 *
 * @param mode enum on rts_detect_mode_t
 *
 * @return RTS_FP_SUCCESS on success.
 * @return RTS_ERROR_TIMEOUT on operation timeout event occurred.
 * @return RTS_ERROR_IO_EXCEPTION on SPI transfer fail.
 */
rts_fp_return_t rts5817maa_sensor_configure_detect(rts_detect_mode_t mode);

/**
 * Returns the status of the finger on the sensor.
 *
 * @return RTS_FINGER_NONE on finger is not present
 * @return RTS_FINGER_PRESENT on finger is present
 */
rts_finger_sts_t rts5817maa_sensor_finger_status(void);

/*
 * @brief Initializes biometric algorithm library.
 *
 * @return RTS_FP_SUCCESS on success
 * @return RTS_ERROR_UNKNOWN on initialize failure.
 */
rts_fp_return_t rts_bio_algorithm_init(void);

/*
 * @brief Instructs the biometric library to release all resources in preparation
 * for the process termination (or unloading the library). Regardless of
 * the returned error code the action is considered unrecoverable.
 *
 */
void rts_bio_algorithm_exit(void);

/*
 * @brief Start fingerprint enrollment and initialize enroll state.
 *
 * @return RTS_FP_SUCCESS on success
 * @return RTS_ERROR_INVALID_PARAMETER on memory alloc failure.
 */
rts_fp_return_t rts_bio_enroll_start(void);

/**
 * @brief Once fingerprint image to the current enrollment session.
 *
 * @param image Current enrollment image.
 * @param completion The percentage of the enrollment process that is
 * complete: [0-100].
 *
 * @return RTS_FP_SUCCESS on success
 * @return RTS_FP_INTERNAL_ERROR indicate internal progress error.
 * @return RTS_FP_POOR_QUALITY current fingerprint image cannot be used because of
 * the poor quality.
 * @return RTS_FP_TOO_HIGH direction information, effecive part of image is only
 * on the up side.
 * @return RTS_FP_TOO_LOW direction information, effecive part of image is only
 * on the down side.
 * @return RTS_FP_TOO_LEFT direction information, effecive part of image is only
 * on the left side.
 * @return RTS_FP_TOO_RIGHT direction information, effecive part of image is only
 * on the right side.
 * @return RTS_FP_IMMOBILE current image is same to previous image.
 */
rts_fp_return_t rts_bio_enroll_step(const uint8_t *const image, int *completion);

/*
 * @brief Indicates that given enrollment process is complete.
 *
 * Algorithm library should generate an active template. After the template is created
 * the library should release all resources associated with this enrollment.
 *
 * @param templ current enrollment active template.
 *
 */
void rts_bio_enroll_finish(void *templ);

/*
 * @brief Compares given biometric image against a list of enrolled template(s).
 * In case the image match a template the match_index will indicate which
 * template in the list that matched.
 * The algorithm library can update templates with additional biometric data
 * from the image, if it chooses to do so. The updated template(s) will be
 * indicated by the out parameter 'update_bitmap', a bit-field where
 * updated template(s) indicated by the corresponding bit being set
 *
 * @param templ Pointer to the array of template buffers.
 * @param templ_count The number of buffers in 'templ'.
 * @param image Pointer to finger image data.
 * @param template_update Whether the template shouldbe updated after successful match.
 * @param match_index Pointer to variable where index of matched template will be stored.
 * @param update_bitmap Pointer to variable where bitmap of update templates.
 *
 * @return RTS_FP_SUCCESS Match success and donnot need to update template.
 * @return RTS_FP_INTERNAL_ERROR indicate internal progress error.
 * @return RTS_FP_POOR_QUALITY current fingerprint image cannot be used because of
 * the poor quality.
 * @return RTS_FP_TOO_HIGH direction information, effecive part of image is only
 * on the up side.
 * @return RTS_FP_TOO_LOW direction information, effecive part of image is only
 * on the down side.
 * @return RTS_FP_TOO_LEFT direction information, effecive part of image is only
 * on the left side.
 * @return RTS_FP_TOO_RIGHT direction information, effecive part of image is only
 * on the right side.
 * @return RTS_FP_MATCH_UPDATED match success and need to update template.
 * @return RTS_FP_MATCH_FAIL current image match template fail.
 */
rts_fp_return_t rts_bio_match(void *templ, uint32_t templ_count,
			    const uint8_t *const image, bool template_update,
			    int32_t *match_index, uint32_t *update_bitmap);

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_RTS5817MAA_PRIVATE_H_ */
