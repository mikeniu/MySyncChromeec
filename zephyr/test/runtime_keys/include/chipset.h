/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_RUNTIME_KEYS_INCLUDE_CHIPSET_H_
#define PLATFORM_EC_ZEPHYR_TEST_RUNTIME_KEYS_INCLUDE_CHIPSET_H_

enum chipset_shutdown_reason {
	CHIPSET_RESET_KB_WARM_REBOOT,
};

void chipset_reset(enum chipset_shutdown_reason reason);

#endif /* PLATFORM_EC_ZEPHYR_TEST_RUNTIME_KEYS_INCLUDE_CHIPSET_H_ */
