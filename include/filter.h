/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FILTER_H
#define __CROS_EC_FILTER_H

#include "common.h"

struct smooth_exp_t {
	float x;
	float a;
};

/*
 * Exponential smoothing
 * x(n+1) = a*x(n-1) + (1-a)*x(n)
 */
static inline void smooth_exp_init(struct smooth_exp_t *exp, float a, float x0)
{
	exp->a = a;
	exp->x = x0;
}

static inline float smooth_exp_step(struct smooth_exp_t *exp, float x)
{
	float ret;

	ret = exp->a * exp->x + (1.0f - exp->a) * x;
	exp->x = ret;

	return ret;
}

/*
 * Butterworth filter 5th rank
 *
 *        b(0) + b(1)z^(-1) + b(2)z^(-2) + b(3)z^(-3) + b(4)z^(-4)
 * Y(z) = ---------------------------------------------------------- X(z)
 *        a(0) + a(1)z^(-1) + a(2)z^(-2) + a(3)z^(-3) + a(4)z^(-4)
 *
 * a(0)*y(k) =     sum     { b(i)*x(k-i) } -     sum      {a(i)*y(k-i) }
 *             k=0,1,2,3,4                     k=1,2,3,4
 */
#define FILTER_RANK_MAX 5

struct filt_param_t {
	float x;
	float y;
	float a;
	float b;
};

struct filt_iir_t {
	uint8_t rank;
	struct filt_param_t params[FILTER_RANK_MAX + 1];
};

int filter_init(struct filt_iir_t *filter, uint8_t rank, float *a, float *b);
float filter_step(struct filt_iir_t *filter, float x);

#endif
