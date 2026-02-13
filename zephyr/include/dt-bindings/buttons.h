/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DT_BINDINGS_BUTTON_H_
#define DT_BINDINGS_BUTTON_H_

/*
 * Custom input key code for the dedicated recovery button. Zephyr's input
 * subsystem provides standard codes for power and volume keys
 * (INPUT_KEY_POWER, INPUT_KEY_VOLUMEUP, INPUT_KEY_VOLUMEDOWN) but has no
 * recovery key, so a vendor-specific code is defined here.
 */
#define INPUT_KEY_RECOVERY 0xFFFF

#endif /* DT_BINDINGS_BUTTON_H_ */
