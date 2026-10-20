/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_THREADINFO_H_
#define ZEPHYR_SHIM_INCLUDE_THREADINFO_H_

#include <zephyr/shell/shell.h>

/**
 * Dump thread information to the console.
 *
 * @param shell Shell instance for printing, or NULL to print via printk()
 *              (e.g., from an ISR or watchdog warning handler).
 */
void threadinfo_dump(const struct shell *shell);

#endif /* ZEPHYR_SHIM_INCLUDE_THREADINFO_H_ */
