/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio_signal.h"
#include "hooks.h"
#include "host_command.h"

#include <zephyr/drivers/gpio.h>

#include <ap_power/ap_power.h>

static void board_backlight_handler(struct ap_power_ev_callback *cb,
				    struct ap_power_ev_data data)
{
	int value;

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		/* Called on AP S3 -> S0 transition */
		value = 1;
		break;

	case AP_POWER_SUSPEND:
		/* Called on AP S0 -> S3 transition */
		value = 0;
		break;
	}
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), value);
}
AP_POWER_EVENT_CALLBACK_DEFINE(board_backlight_handler, AP_POWER_RESUME,
			       AP_POWER_SUSPEND);

/**
 * Host command to toggle backlight.
 *
 * The requested state will persist until the next lid-switch or request-gpio
 * transition.
 */
static enum ec_host_cmd_status
switch_command_enable_backlight(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_switch_enable_backlight *p = args->input_buf;
	int value = p->enabled;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), value);

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_SWITCH_ENABLE_BKLIGHT,
			     switch_command_enable_backlight, EC_VER_MASK(0),
			     struct ec_params_switch_enable_backlight);
