/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(skywalker_backlight, LOG_LEVEL_INF);

void edp_bl_en_1v8(enum gpio_signal signal)
{
	int state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_bl_en_1v8));
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), state);
}

static void edp_bl_en_1v8_enable(void)
{
	int cbi_boardversion = -1;
	int ret;
	ret = cbi_get_board_version(&cbi_boardversion);
	if ((ret < 0) || (cbi_boardversion == -1)) {
		LOG_ERR("error retrieving board version: %d", ret);
		return;
	}
	if (cbi_boardversion >= 3)
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_edp_bl_en_1v8));
	else
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_edp_bl_en_1v8));
}
DECLARE_HOOK(HOOK_INIT, edp_bl_en_1v8_enable, HOOK_PRIO_DEFAULT);
