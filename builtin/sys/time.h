/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_BUILTIN_SYS_TIME_H_
#define PLATFORM_EC_BUILTIN_SYS_TIME_H_

#include <sys/types.h>

/**
 * Partial implementation of <sys/time.h> header:
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sys_time.h.html
 */

typedef int64_t time_t;
typedef int32_t suseconds_t;

struct timeval {
	time_t tv_sec; /* seconds */
	suseconds_t tv_usec; /* microseconds */
};

#endif /* PLATFORM_EC_BUILTIN_SYS_TIME_H_ */
