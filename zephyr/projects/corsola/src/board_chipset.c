/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola baseboard-chipset specific configuration */

#include <zephyr/init.h>
#include <ap_power/ap_power.h>
#include <zephyr/drivers/gpio.h>

#include "chipset.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"

static void update_backlight(void)
{
	bool enable = lid_is_open() && chipset_in_state(CHIPSET_STATE_ON);

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), enable);
}
DECLARE_HOOK(HOOK_LID_CHANGE, update_backlight, HOOK_PRIO_DEFAULT);

static void board_backlight_handler(struct ap_power_ev_callback *cb,
				    struct ap_power_ev_data data)
{
	update_backlight();
}

static int install_backlight_handler(const struct device *unused)
{
	static struct ap_power_ev_callback cb;

	/*
	 * Add a callback for suspend/resume to
	 * control the keyboard backlight.
	 */
	ap_power_ev_init_callback(&cb, board_backlight_handler,
				  AP_POWER_RESUME | AP_POWER_SUSPEND);
	ap_power_ev_add_callback(&cb);
	return 0;
}

SYS_INIT(install_backlight_handler, APPLICATION, 1);
