/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Meru chipset-specific configuration */

#include "battery.h"
#include "common.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

#ifndef CONFIG_PLATFORM_EC_LIGHTBAR_AC_UNPLUG_DELAY_MS
#define CONFIG_PLATFORM_EC_LIGHTBAR_AC_UNPLUG_DELAY_MS 0
#endif

static void disable_pp5000_s5(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5), 0);
}
DECLARE_DEFERRED(disable_pp5000_s5);

void board_chipset_startup_meru(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup_meru,
	     HOOK_PRIO_DEFAULT);

void board_chipset_shutdown_meru(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown_meru,
	     HOOK_PRIO_DEFAULT);

void s3_power_interrupt(enum gpio_signal signal)
{
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3),
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pp1800_l1i_s3_ec)));
}

static void enable_s3_interrupt(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_s3_power_monitor));
}
DECLARE_HOOK(HOOK_INIT, enable_s3_interrupt, HOOK_PRIO_DEFAULT);

static void board_chipset_pre_init_meru(void)
{
	hook_call_deferred(&disable_pp5000_s5_data, -1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5), 1);

	if (battery_is_present() != BP_YES) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira),
				1);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_chipset_pre_init_meru,
	     HOOK_PRIO_DEFAULT);

static void board_chipset_hard_off_meru(void)
{
	hook_call_deferred(
		&disable_pp5000_s5_data,
		(5000 + CONFIG_PLATFORM_EC_LIGHTBAR_AC_UNPLUG_DELAY_MS) *
			USEC_PER_MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, board_chipset_hard_off_meru,
	     HOOK_PRIO_DEFAULT);
