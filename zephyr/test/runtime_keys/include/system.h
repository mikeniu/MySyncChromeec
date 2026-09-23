/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_RUNTIME_KEYS_INCLUDE_SYSTEM_H_
#define PLATFORM_EC_ZEPHYR_TEST_RUNTIME_KEYS_INCLUDE_SYSTEM_H_

#include <stdint.h>

void system_enter_hibernate(uint32_t seconds, uint32_t microseconds);

#endif /* PLATFORM_EC_ZEPHYR_TEST_RUNTIME_KEYS_INCLUDE_SYSTEM_H_ */
