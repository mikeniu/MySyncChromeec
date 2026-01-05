/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BTN_IGN_IN_H
#define __CROS_EC_BTN_IGN_IN_H

/**
 * Handle button ignore logic for power button.
 *
 * @param new_pressed   Current raw power button state (1 for pressed, 0 for
 * released).
 * @return Remaining microseconds to ignore the press.
 */
int btn_ign_in_power_pressed(int new_pressed);

#endif /* __CROS_EC_BTN_IGN_IN_H */
