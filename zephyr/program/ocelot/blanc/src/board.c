/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"

LOG_MODULE_REGISTER(hekla_board, LOG_LEVEL_INF);

static void bl_enable(void)
{
	if (lid_is_open() && !chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_edp_bl), true);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_edp_bl), false);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, bl_enable, HOOK_PRIO_DEFAULT);
