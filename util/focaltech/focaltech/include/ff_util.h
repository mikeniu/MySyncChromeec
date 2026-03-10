/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __FF_UTIL_H__
#define __FF_UTIL_H__

<<<<<<< PATCH SET (20382701f0d1b80db4dfe5c816c7c0450f8da27c     1.Re-implement the command parsing logic.)
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
||||||| BASE      (2e6c46277614b9605a2c5ae9eba08e026a406e90 pdc: Add "USB Device Capable" to pdc info)
=======
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dump the memory content to logcat like 'hexdump -C'.
 *
 * @params
 *  buf: Memory buffer.
 *  len: Buffer length.
 */
void ff_util_hexdump(uint8_t mak, const char *info, const uint8_t *buf,
		     uint32_t len);
>>>>>>> BASE      (fcd9064294552a3cbf330ce87121caabb6c418c8 util: focaltech: Add focaltech IAP tool)

#ifdef __cplusplus
}
#endif

#endif //__DUMP_H__
