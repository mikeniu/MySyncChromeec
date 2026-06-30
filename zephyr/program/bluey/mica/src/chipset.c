/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mica chipset-specific configuration */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "power/qcom.h"

/* true while the AP power-on sequence is in progress. */
static bool power_on_in_progress;

static void disable_pp5000(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 0);
}
DECLARE_DEFERRED(disable_pp5000);

void board_chipset_startup_mica(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl), 1);

	power_on_in_progress = false;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup_mica,
	     HOOK_PRIO_DEFAULT);

void board_chipset_shutdown_mica(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_batt_i2c_en_odl), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown_mica,
	     HOOK_PRIO_DEFAULT);

static void board_chipset_pre_init_mica(void)
{
	hook_call_deferred(&disable_pp5000_data, -1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 1);

	power_on_in_progress = true;
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_chipset_pre_init_mica,
	     HOOK_PRIO_DEFAULT);

static void board_chipset_hard_off_mica(void)
{
	enum power_on_event_t power_on_reason;

	power_on_reason = chipset_get_power_on_reason();
	if (power_on_in_progress && power_on_reason != POWER_ON_BY_AC_ON) {
		board_diag_led_power_fail();
		/* Keep the PP5000 power rail enabled for 10 minutes after
		 * lightbar diagnostic for a power failure.
		 */
		hook_call_deferred(&disable_pp5000_data,
				   600000 * USEC_PER_MSEC);
	} else {
		hook_call_deferred(
			&disable_pp5000_data,
			(5000 + CONFIG_CROS_EC_LIGHTBAR_AC_UNPLUG_DELAY_MS) *
				USEC_PER_MSEC);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, board_chipset_hard_off_mica,
	     HOOK_PRIO_DEFAULT);

void board_chipset_suspend_mica(void)
{
	/* Reduces suspend power consumption by disable panel bl power. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend_mica,
	     HOOK_PRIO_DEFAULT);

void board_chipset_resume_mica(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume_mica, HOOK_PRIO_DEFAULT);
