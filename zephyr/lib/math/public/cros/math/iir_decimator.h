/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_LIB_MATH_PUBLIC_CROS_MATH_IIR_DECIMATOR_H_
#define PLATFORM_EC_ZEPHYR_LIB_MATH_PUBLIC_CROS_MATH_IIR_DECIMATOR_H_

#include "cros/math/iir_filter.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IIR decimator state for 3 axes.
 *
 * An IIR decimator applies a low-pass filter to a signal and then downsamples
 * it. This is useful for reducing the sampling rate of a signal while
 * preventing aliasing.
 *
 * This implementation applies the same filter and decimation to three
 * independent channels (e.g., x, y, and z axes of a sensor).
 */
struct iir_decimator_t {
	/** The desired output frequency in mHz after decimation. */
	const uint32_t optimal_frequency_mhz;
	/** The rank of the Butterworth low-pass filter. */
	const int filter_rank;
	/**
	 * The factor by which the signal is downsampled. Calculated during
	 * initialization based on the sample rate and optimal frequency.
	 */
	uint32_t decimation_factor;
	/** Internal counter for decimation. */
	uint32_t decimation_count;
	/** IIR filter for the first channel (e.g., x-axis). */
	struct iir_filter_t x;
	/** IIR filter for the second channel (e.g., y-axis). */
	struct iir_filter_t y;
	/** IIR filter for the third channel (e.g., z-axis). */
	struct iir_filter_t z;
};

/**
 * @brief Macro to declare and initialize an iir_decimator_t struct.
 *
 * @param name_ The name of the struct variable.
 * @param optimal_frequency_mhz_ The desired output frequency in Hz.
 * @param filter_rank_ The rank of the Butterworth low-pass filter.
 */
#define IIR_DECIMATOR(name_, optimal_frequency_mhz_, filter_rank_) \
	struct iir_decimator_t name_ = {                           \
		.optimal_frequency_mhz = optimal_frequency_mhz_,   \
		.filter_rank = filter_rank_,                       \
		.decimation_factor = 0,                            \
		.decimation_count = 0,                             \
		.x = { .rank = 0, .params = {}, },                 \
		.y = { .rank = 0, .params = {}, },                 \
		.z = { .rank = 0, .params = {}, },                 \
	}

/**
 * @brief Initializes an IIR decimator.
 *
 * This function calculates the decimation factor based on the input sample
 * rate and the desired optimal frequency. It then initializes the internal
 * low-pass filters.
 *
 * @param decimator Pointer to the iir_decimator_t struct.
 * @param sample_rate_mhz The input sample rate in mHz.
 * @return 0 on success, or a negative error code on failure.
 */
int iir_decimator_init(struct iir_decimator_t *decimator, int sample_rate_mhz);

/**
 * @brief Processes one sample through the IIR decimator.
 *
 * This function applies the low-pass filter to the input sample (x, y, z).
 * It then checks if the sample should be kept or discarded based on the
 * decimation factor.
 *
 * @param decimator Pointer to the iir_decimator_t struct.
 * @param x Pointer to the input/output for the first channel.
 * @param y Pointer to the input/output for the second channel.
 * @param z Pointer to the input/output for the third channel.
 * @return True if the sample is a valid output sample (should be used),
 *         false if the sample should be skipped (decimated).
 */
bool iir_decimator_step(struct iir_decimator_t *decimator, float *x, float *y,
			float *z);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_LIB_MATH_PUBLIC_CROS_MATH_IIR_DECIMATOR_H_ */
