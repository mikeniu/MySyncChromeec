/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __FF_UTIL_H__
#define __FF_UTIL_H__

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

#ifdef __cplusplus
}
#endif

#endif //__DUMP_H__
