/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_UART_PRINTF_INCLUDE_PANIC_LOG_H_
#define PLATFORM_EC_ZEPHYR_TEST_UART_PRINTF_INCLUDE_PANIC_LOG_H_

#include <stddef.h>

void panic_log_write_char(const char c);
void panic_log_write_str(const char *str, size_t size);

#endif /* PLATFORM_EC_ZEPHYR_TEST_UART_PRINTF_INCLUDE_PANIC_LOG_H_ */
