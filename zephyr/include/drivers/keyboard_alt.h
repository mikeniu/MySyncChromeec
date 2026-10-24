/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_KEYBOARD_ALT_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_KEYBOARD_ALT_H_

#include <stdbool.h>

/*
 * Switch to the alt-* key positions from devicetree, properties that are not
 * defined keep the main value. Must be called before HOOK_INIT_EARLY, for
 * example from a SYS_INIT() at APPLICATION level.
 */
void boot_keys_use_alternate(void);
void runtime_keys_use_alternate(void);
void fn_keys_use_alternate(void);

/* Return false to skip the ghost filter masks, defaults to true. */
bool board_ghost_filter_enabled(void);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_KEYBOARD_ALT_H_ */
