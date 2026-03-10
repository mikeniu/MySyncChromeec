/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __FF_UTIL_H__
#define __FF_UTIL_H__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Safely calculates the ceiling division of n / d */
static inline uint32_t ft_div_round_up(uint32_t n, uint32_t d)
{
	assert(d != 0);
	return (n + d - 1) / d;
}

#ifdef __cplusplus
}
#endif

#endif //__DUMP_H__
