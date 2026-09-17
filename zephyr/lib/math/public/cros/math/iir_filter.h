/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Infinite Impulse Response (IIR) and Exponential Smoothing Filters
 *
 * This header provides functions for initializing and using IIR filters. It
 * includes support for Butterworth low-pass filters.
 */
#ifndef PLATFORM_EC_ZEPHYR_LIB_MATH_PUBLIC_CROS_MATH_IIR_FILTER_H_
#define PLATFORM_EC_ZEPHYR_LIB_MATH_PUBLIC_CROS_MATH_IIR_FILTER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum supported filter rank.
 */
#define FILTER_RANK_MAX 5

/**
 * @brief Factors for Butterworth filters up to FILTER_RANK_MAX.
 *
 * The transformation of the filter can be expressed as a product of
 * transformations of 2nd order filters. Each sub-filter is in the form:
 * @code
 *                  1
 *   H(s) = --------------------
 *           (s^2 + As^1 + 1)
 * @endcode
 * The A coefficients are provided below as int16_t fixed precision
 * expressed in 0.0001f units.
 *
 * All coefficients are placed in order of N:K, where N is rank and K
 * is the iterator of the sub-filter. For a 5th order filter, the format is:
 * {2:1,  3:1,  4:1,  4:2,  5:1,  5:2}
 *
 * If the filter rank is odd, there is no need to store an additional sub-filter
 * coefficient as it is known to be:
 * @code
 *           1
 *   H(s) = -----
 *         s + 1
 * @endcode
 *
 * @see https://en.wikipedia.org/wiki/Butterworth_filter
 */
#define FILTER_BUTTERWORTH_FACTORS { 14142, 10000, 7654, 18478, 6180, 16180 }
#define FILTER_BUTTERWORTH_FACTOR_SCALE 0.0001f

/**
 * @brief Parameters for a single stage of an IIR filter.
 */
struct iir_filter_params_t {
	/** Input history */
	float x;
	/** Output history */
	float y;
	/** 'a' coefficient */
	float a;
	/** 'b' coefficient */
	float b;
};

/**
 * @brief Nth-rank Infinite Impulse Response (IIR) filter structure.
 *
 * The filter is defined by the transfer function:
 * @code
 *         b(0) + b(1)z^(-1) + ... + b(n)z^(-(n-1))
 * Y(z) = ------------------------------------------ X(z)
 *         a(0) + a(1)z^(-1) + ... + a(n)z^(-(n-1))
 * @endcode
 *
 * The difference equation is:
 * @code
 * a(0)*y(k) = sum_{i=0}^{n-1} {b(i)*x(k-i)} - sum_{i=1}^{n-1} {a(i)*y(k-i)}
 * @endcode
 */
struct iir_filter_t {
	/** The rank of the filter. */
	uint8_t rank;
	/** Filter parameters for each stage. */
	struct iir_filter_params_t params[FILTER_RANK_MAX + 1];
};

/**
 * @brief Initializes a generic IIR filter.
 *
 * @param filter Pointer to the IIR filter structure.
 * @param rank The rank of the filter (up to FILTER_RANK_MAX).
 * @param a Array of 'a' coefficients.
 * @param b Array of 'b' coefficients.
 * @return 0 on success, error code on failure.
 */
int iir_filter_init(struct iir_filter_t *filter, uint8_t rank, float *a,
		    float *b);

/**
 * @brief Performs one step of the IIR filter.
 *
 * @param filter Pointer to the IIR filter structure.
 * @param x The new input value.
 * @return The next filtered output value.
 */
float iir_filter_step(struct iir_filter_t *filter, float x);

/**
 * @brief Initializes a Butterworth low-pass filter.
 *
 * @param filter Pointer to the IIR filter structure.
 * @param rank The rank of the filter (up to FILTER_RANK_MAX).
 * @param freq_lowpass The low-pass cutoff frequency.
 * @return 0 on success, error code on failure.
 */
int filter_butterworth_lpf_init(struct iir_filter_t *filter, uint8_t rank,
				float freq_lowpass);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_LIB_MATH_PUBLIC_CROS_MATH_IIR_FILTER_H_ */
