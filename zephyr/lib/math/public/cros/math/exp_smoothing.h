/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Exponential Smoothing Filter
 *
 * This header provides functions for initializing and using an exponential
 * smoothing filter.
 */

#ifndef PLATFORM_EC_ZEPHYR_LIB_MATH_PUBLIC_CROS_MATH_EXP_SMOOTHING_H_
#define PLATFORM_EC_ZEPHYR_LIB_MATH_PUBLIC_CROS_MATH_EXP_SMOOTHING_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Structure for exponential smoothing.
 */
struct exp_smooth_t {
	float x; /**< Current smoothed value */
	float a; /**< Smoothing factor */
	float a_complementary; /**< 1.0f - a */
	bool is_valid; /**< True if the filter has been initialized */
};

/**
 * @brief Initializes an exponential smoothing filter.
 *
 * The smoothing formula is: y(n+1) = a*y(n) + (1-a)*x(n)
 *
 * @param exp Pointer to the exponential smoothing structure.
 * @param a Smoothing factor (0.0f to 1.0f).
 * @param x0 Initial value.
 */
static inline void exp_smooth_init(struct exp_smooth_t *exp, float a, float x0)
{
	exp->a = a;
	exp->a_complementary = 1.0f - a;
	exp->x = x0;
}

/**
 * @brief Performs one step of exponential smoothing.
 *
 * @param exp Pointer to the exponential smoothing structure.
 * @param x The new input value.
 * @return The next smoothed value.
 */
static inline float exp_smooth_step(struct exp_smooth_t *exp, float x)
{
	float ret;

	ret = exp->a * exp->x + exp->a_complementary * x;
	exp->x = ret;

	return ret;
}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_LIB_MATH_PUBLIC_CROS_MATH_EXP_SMOOTHING_H_ */
