/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistence module for emulator */

#ifndef PLATFORM_EC_CHIP_HOST_PERSISTENCE_H_
#define PLATFORM_EC_CHIP_HOST_PERSISTENCE_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

FILE *get_persistent_storage(const char *tag, const char *mode);

void release_persistent_storage(FILE *ps);

void remove_persistent_storage(const char *tag);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_CHIP_HOST_PERSISTENCE_H_ */
