/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_PANIC_LOG_H_
#define PLATFORM_EC_INCLUDE_PANIC_LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* Write a character to the panic log. Character will be dropped if panic log is
 * frozen. */
void panic_log_write_char(const char c);

/* Write a string to the panic log. String will be dropped if panic log is
 * frozen. */
void panic_log_write_str(const char *str, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_PANIC_LOG_H_ */
