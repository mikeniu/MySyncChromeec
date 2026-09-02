/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Input devices using Matrix Keyboard Protocol [MKBP] events for Chrome EC */

#ifndef PLATFORM_EC_INCLUDE_MKBP_INPUT_DEVICES_H_
#define PLATFORM_EC_INCLUDE_MKBP_INPUT_DEVICES_H_

#include "common.h"
#include "ec_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Update the state of buttons
 *
 * @param button	The button that changed.
 * @param is_pressed	Whether the button is now pressed.
 */
void mkbp_button_update(enum keyboard_button_type button, int is_pressed);

/**
 * Retrieve state of buttons [Power, Volume up/down, etc]
 */
uint32_t mkbp_get_button_state(void);

/**
 * Retrieve state of switches [Lid open/closed, tablet mode switch, etc]
 */
uint32_t mkbp_get_switch_state(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_MKBP_INPUT_DEVICES_H_ */
