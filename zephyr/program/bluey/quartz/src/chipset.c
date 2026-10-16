/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quartz chipset-specific configuration */

#include "battery.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "power.h"
#include "system.h"
#include "task.h"

#include <zephyr/kernel.h>

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

#ifndef CONFIG_PLATFORM_EC_LIGHTBAR_AC_UNPLUG_DELAY_MS
#define CONFIG_PLATFORM_EC_LIGHTBAR_AC_UNPLUG_DELAY_MS 0
#endif

/* Time to wait for PP5000_S5 power good */
#define PP5000_S5_PG_TIMEOUT_MS 50

/*
 * PP5000_S5 is shared by several components, so it is requested through
 * power_5v_enable(). Wait for power good when turning it on.
 */
__override void board_power_5v_enable(int enable)
{
	const struct gpio_dt_spec *en =
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5);
	const struct gpio_dt_spec *pg =
		GPIO_DT_FROM_NODELABEL(gpio_pg_pp5000_s5_od);
	int i;

	if (!enable) {
		gpio_pin_set_dt(en, 0);
		return;
	}

	if (gpio_pin_get_dt(en))
		return;

	gpio_pin_set_dt(en, 1);
	for (i = 0; i < PP5000_S5_PG_TIMEOUT_MS && !gpio_pin_get_dt(pg); i++)
		k_msleep(1);
	if (!gpio_pin_get_dt(pg))
		CPRINTS("PP5000_S5 power good timeout");
}

static void disable_pp5000_s5(void)
{
	power_5v_enable(TASK_ID_CHIPSET, 0);
}
DECLARE_DEFERRED(disable_pp5000_s5);

void board_chipset_startup_quartz(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup_quartz,
	     HOOK_PRIO_DEFAULT);

void board_chipset_shutdown_quartz(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown_quartz,
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

static void board_chipset_pre_init_quartz(void)
{
	hook_call_deferred(&disable_pp5000_s5_data, -1);
	power_5v_enable(TASK_ID_CHIPSET, 1);

	if (battery_is_present() != BP_YES) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira),
				1);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_chipset_pre_init_quartz,
	     HOOK_PRIO_DEFAULT);

/*
 * After a sysjump with the AP on, HOOK_CHIPSET_PRE_INIT is not called
 * again. Re-assert the chipset's 5V request after restore_enable_5v_state()
 * in case the previous image did not preserve the request bitmask.
 */
static void board_pp5000_s5_init(void)
{
	if (system_jumped_to_this_image() &&
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_s4c_pwr_detect)))
		power_5v_enable(TASK_ID_CHIPSET, 1);
}
DECLARE_HOOK(HOOK_INIT, board_pp5000_s5_init, HOOK_PRIO_POST_FIRST);

static void board_chipset_hard_off_quartz(void)
{
	hook_call_deferred(
		&disable_pp5000_s5_data,
		(5000 + CONFIG_PLATFORM_EC_LIGHTBAR_AC_UNPLUG_DELAY_MS) *
			USEC_PER_MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, board_chipset_hard_off_quartz,
	     HOOK_PRIO_DEFAULT);
