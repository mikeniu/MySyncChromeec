/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_PANIC_LOG_H_
#define PLATFORM_EC_INCLUDE_PANIC_LOG_H_

#include "config.h"

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/ring_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Update if the schema of the panic log changes */
#define PANIC_LOG_VERSION 1

#define PANIC_LOG_START_MAGIC 0x504c4f47 /* 'PLOG' */
#define PANIC_LOG_END_MAGIC 0x474f4c50 /* 'GOLP' */

#ifdef CONFIG_PANIC_LOG_SIZE
struct panic_log_storage {
	uint32_t start_magic;
	uint32_t version;
	struct ring_buf rb;
	uint8_t buffer[CONFIG_PANIC_LOG_SIZE];
	uint32_t end_magic;
};
#endif

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
