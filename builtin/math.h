/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef BUILTIN_MATH_H_
#define BUILTIN_MATH_H_

#include "fpu.h"

#include <stdbool.h>

static inline bool isnan(float a)
{
	return __builtin_isnan(a);
}

static inline bool isinf(float a)
{
	return __builtin_isinf(a);
}

#endif /* BUILTIN_MATH_H_ */
