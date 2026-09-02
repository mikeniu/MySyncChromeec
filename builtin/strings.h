/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_BUILTIN_STRINGS_H_
#define PLATFORM_EC_BUILTIN_STRINGS_H_

#include <stddef.h>

int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t size);

#endif /* PLATFORM_EC_BUILTIN_STRINGS_H_ */
