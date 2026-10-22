/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_BOOT_KEYS_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_BOOT_KEYS_H_

#include <stdint.h>

#include <zephyr/devicetree.h>

/* Select the active boot keys configuration, called by boot_keys.c when
 * processing the first key event if more than one configuration is defined.
 */
int8_t board_boot_keys_idx(void);

#define BOOT_KEYS_CFG_IDX(nodelabel) DT_NODE_CHILD_IDX(DT_NODELABEL(nodelabel))

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_BOOT_KEYS_H_ */
