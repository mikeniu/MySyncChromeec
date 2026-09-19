/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "lid_switch.h"
#include "mock_input.h"
#include "power_button.h"

#include <stdbool.h>

#include <zephyr/fff.h>

DEFINE_FAKE_VALUE_FUNC(int, power_button_is_pressed);
DEFINE_FAKE_VALUE_FUNC(int, lid_is_open);

void mock_power_button_set(bool pressed)
{
	power_button_is_pressed_fake.return_val = pressed;
	hook_notify(HOOK_POWER_BUTTON_CHANGE);
}

void mock_lid_set(bool open)
{
	lid_is_open_fake.return_val = open;
	hook_notify(HOOK_LID_CHANGE);
}

void mock_input_reset_state(void)
{
	RESET_FAKE(power_button_is_pressed);
	RESET_FAKE(lid_is_open);
	lid_is_open_fake.return_val = 1;
}
