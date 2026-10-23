/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_PANIC_UTILS_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_PANIC_UTILS_H_

#include <zephyr/kernel.h>

/**
 * @brief Print the stack back trace for the specified thread
 *
 * This function does nothing if CONFIG_ARCH_STACKWALK is disabled.
 *
 * @param thread Thread id to print the stack back trace
 */
void print_stack_trace(const struct k_thread *thread);

/**
 * @brief Get the stack pointer for the specified thread.
 *
 * @param thread Kernel thread
 * @returns 32-bit stack pointer for the thread.
 */
uint32_t get_stack_ptr(const struct k_thread *thread);

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_PANIC_UTILS_H_ */
