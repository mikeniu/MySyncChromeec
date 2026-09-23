/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_TOUCHPAD_H_
#define PLATFORM_EC_INCLUDE_TOUCHPAD_H_

void touchpad_interrupt(enum gpio_signal signal);

/* Reset the touchpad, mainly used to recover it from malfunction. */
void board_touchpad_reset(void);

#endif /* PLATFORM_EC_INCLUDE_TOUCHPAD_H_ */
