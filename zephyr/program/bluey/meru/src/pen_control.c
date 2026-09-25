/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "peripheral_charger.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pen_control, LOG_LEVEL_ERR);

__override void board_pchg_power_on(int port, bool on)
{
	if (port != 0)
		return;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_wlc_x), on);
}
