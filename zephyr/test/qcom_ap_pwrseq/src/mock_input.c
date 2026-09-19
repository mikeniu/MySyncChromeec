/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "lid_switch.h"
#include "mock_input.h"
#include "power_button.h"

#include <stdbool.h>

bool mock_power_button_pressed;
bool mock_lid_open;

void unused_hook_HOOK_POWER_BUTTON_CHANGE_qcom_power_button_changed(void);
void unused_hook_HOOK_LID_CHANGE_lid_state_change(void);

int power_button_is_pressed(void)
{
	return mock_power_button_pressed;
}

int lid_is_open(void)
{
	return mock_lid_open;
}

void mock_power_button_set(bool pressed)
{
	mock_power_button_pressed = pressed;
	unused_hook_HOOK_POWER_BUTTON_CHANGE_qcom_power_button_changed();
}

void mock_lid_set(bool open)
{
	mock_lid_open = open;
	unused_hook_HOOK_LID_CHANGE_lid_state_change();
}
