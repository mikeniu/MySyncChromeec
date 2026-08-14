/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pic chipset-specific configuration */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "power/qcom.h"

static void disable_pp5000(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 0);
}
DECLARE_DEFERRED(disable_pp5000);

void board_chipset_startup_pic(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup_pic,
	     HOOK_PRIO_DEFAULT);

void board_chipset_shutdown_pic(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl), 0);

	/* Release system power throttling limit (PROCHOT) during shutdown. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_sys_throttle_mira), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown_pic,
	     HOOK_PRIO_DEFAULT);

static void board_chipset_hard_off_pic(void)
{
	hook_call_deferred(&disable_pp5000_data,
			   (5000 + CONFIG_CROS_EC_LIGHTBAR_AC_UNPLUG_DELAY_MS) *
				   USEC_PER_MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, board_chipset_hard_off_pic,
	     HOOK_PRIO_DEFAULT);

void board_chipset_suspend_pic(void)
{
	/* Reduces suspend power consumption by disable panel bl power. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend_pic,
	     HOOK_PRIO_DEFAULT);

void board_chipset_resume_pic(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume_pic, HOOK_PRIO_DEFAULT);

/* Trigger Zoss coverage check */
