/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "lid_switch.h"

static void bl_enable(void)
{
	if (lid_is_open() && !chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_edp_bl), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_edp_bl), 0);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, bl_enable, HOOK_PRIO_DEFAULT);

static void touchpad_enable(void)
{
	if (lid_is_open() && !chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tpad_en), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tpad_en), 0);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, touchpad_enable, HOOK_PRIO_DEFAULT);
