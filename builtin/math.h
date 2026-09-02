/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_BUILTIN_MATH_H_
#define PLATFORM_EC_BUILTIN_MATH_H_

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

#endif /* PLATFORM_EC_BUILTIN_MATH_H_ */
