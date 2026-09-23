/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_BUILTIN_SYS_TYPES_H_
#define PLATFORM_EC_BUILTIN_SYS_TYPES_H_

/* Data type for POSIX style clock() implementation */
typedef long clock_t;

/* There is a GCC macro for a size_t type, but not for a ssize_t type.
 * The following construct convinces GCC to make __SIZE_TYPE__ signed.
 */
#define unsigned signed
typedef __SIZE_TYPE__ ssize_t;
#undef unsigned

#endif /* PLATFORM_EC_BUILTIN_SYS_TYPES_H_ */
