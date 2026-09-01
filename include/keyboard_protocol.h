/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Keyboard protocol interface
 */

#ifndef __CROS_EC_KEYBOARD_PROTOCOL_H
#define __CROS_EC_KEYBOARD_PROTOCOL_H

#include "common.h"

/* Protocol-specific includes */

#ifdef CONFIG_KEYBOARD_PROTOCOL_8042
#include "keyboard_8042.h"
#endif

#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
#include "keyboard_mkbp.h"

/* MKBP protocol takes the whole keyboard matrix, and does not care about
 * individual key presses.
 */
static inline void keyboard_state_changed(int row, int col, int is_pressed) {}
#else
/**
 * Called by keyboard scan code once any key state change (after de-bounce),
 *
 * This function will look up matrix table and convert scancode host.
 */
void keyboard_state_changed(int row, int col, int is_pressed);
#endif

/**
 * Returns true if keyboard backlight is present/detected.
 */
int board_has_keyboard_backlight(void);

#endif  /* __CROS_EC_KEYBOARD_PROTOCOL_H */
