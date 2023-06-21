/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "filter.h"

#include <stddef.h>

int filter_init(struct filt_iir_t *filter, uint8_t rank, float *a, float *b)
{
	uint8_t i;

	if (filter == NULL || (rank > FILTER_RANK_MAX))
		return -1;

	filter->rank = rank;
	for (i = 0; i < rank + 1; i++) {
		filter->params[i].x = 0.0f;
		filter->params[i].y = 0.0f;
		filter->params[i].a = a[i];
		filter->params[i].b = b[i];
	}

	return 0;
}

float filter_step(struct filt_iir_t *filter, float x)
{
	uint8_t i;
	float val;

	/* Shift x and y values */
	for (i = (filter->rank); i > 0; i--) {
		filter->params[i].x = filter->params[i - 1].x;
		filter->params[i].y = filter->params[i - 1].y;
	}
	filter->params[0].x = x;
	filter->params[0].y = 0.0f;

	val = 0.0f;

	for (i = 0; i < filter->rank + 1; i++) {
		val += filter->params[i].b * filter->params[i].x;
		val -= filter->params[i].a * filter->params[i].y;
	}

	val = val / filter->params[0].a;
	filter->params[0].y = val;

	return val;
}
