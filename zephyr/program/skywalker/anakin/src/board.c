/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "chipset.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "timer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_INF);

#define INT_RECHECK_US 5000

static void board_backlight_handler(struct ap_power_ev_callback *cb,
				    struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

	case AP_POWER_STARTUP:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_blpwr),
				1);
		break;

	case AP_POWER_HARD_OFF:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_blpwr),
				0);
		break;
	}
}

static int install_backlight_handler(void)
{
	static struct ap_power_ev_callback cb;
	/*
	 * Add a callback for start/hardoff to
	 * control the backlight load switch.
	 */
	ap_power_ev_init_callback(&cb, board_backlight_handler,
				  AP_POWER_STARTUP | AP_POWER_HARD_OFF);
	ap_power_ev_add_callback(&cb);

	return 0;
}

SYS_INIT(install_backlight_handler, APPLICATION, 1);

static void check_audio_jack(void)
{
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON)) {
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_jd1)))
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_5p0va_pwr_mode), 0);
		else
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_5p0va_pwr_mode), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_5p0va_pwr_mode), 0);
	}
}
DECLARE_DEFERRED(check_audio_jack);

DECLARE_HOOK(HOOK_INIT, check_audio_jack, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, check_audio_jack, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, check_audio_jack, HOOK_PRIO_DEFAULT);

void audio_jack_interrupt(enum gpio_signal s)
{
	hook_call_deferred(&check_audio_jack_data, INT_RECHECK_US);
}

static void board_setup_init()
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_jd1));
}
DECLARE_HOOK(HOOK_INIT, board_setup_init, HOOK_PRIO_PRE_DEFAULT);

#ifndef CONFIG_BATTERY_PRESENT_GPIO
static enum battery_present battery_is_present_gpio(void)
{
	/* The GPIO is low when the battery is present */
	return gpio_get_level(GPIO_BATT_PRES_ODL) ? BP_NO : BP_YES;
}
static enum battery_present battery_is_present_smart(void)
{
	int manufacture_date;

	if (sb_read(SB_MANUFACTURE_DATE, &manufacture_date)) {
		/* Require 2 consecutive failures before declaring the
		 * battery missing.
		 */
		k_busy_wait(1 * USEC_PER_MSEC);
		if (sb_read(SB_MANUFACTURE_DATE, &manufacture_date)) {
			return BP_NOT_SURE;
		}
	}

	return BP_YES;
}
enum battery_present battery_is_present(void)
{
	static const char *const batt_pres[] = {
		"NO",
		"YES",
		"NOT_SURE",
	};
	static atomic_t a_number_of_mismatch = ATOMIC_INIT(0);
	static atomic_t a_call_count = ATOMIC_INIT(0);
	static atomic_t a_last_state = ATOMIC_INIT(false);

	enum battery_present smart_result = battery_is_present_smart();
	enum battery_present gpio_result = battery_is_present_gpio();
	bool mismatch = smart_result != gpio_result;
	bool last_state = atomic_set(&a_last_state, mismatch);
	atomic_val_t number_of_mismatch =
		atomic_add(&a_number_of_mismatch, mismatch) + mismatch;
	atomic_val_t call_count = atomic_inc(&a_call_count) + 1;

	if (call_count % 256 == 0 || last_state != mismatch) {
		LOG_INF("battery_is_present: called %ld times, "
			"mismatch %ld times, smart=%s %s gpio=%s "
			"===============================",
			call_count, number_of_mismatch, batt_pres[smart_result],
			mismatch ? "!=" : "==", batt_pres[gpio_result]);
	}

	return smart_result;
}
#endif
