/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_UTIL_UART_UTILS_H_
#define PLATFORM_EC_UTIL_UART_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Discards (flushes) any data received via UART, but not yet retrieved.
 *
 * @param fd File descriptor of the UART device.
 * @return Number of bytes discarded.
 */
int uart_flush(int fd);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_UTIL_UART_UTILS_H_ */
