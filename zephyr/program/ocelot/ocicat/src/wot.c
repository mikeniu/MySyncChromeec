/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "chipset.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_backlight.h"
#include "keyboard_config.h"
#include "lid_switch.h"
#include "power_button.h"
#include "tablet_mode.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power_events.h>

LOG_MODULE_DECLARE(board_init, LOG_LEVEL_INF);

#define TS_WOT_DELAY_MS (100 * USEC_PER_MSEC)
#define PWR_BTN_DURATION 200

#define STATE_UNINITIALIZED (-1)

static int prev_backlight_on = STATE_UNINITIALIZED;

static inline bool ts_get_tablet_mode(void)
{
	return tablet_get_mode();
}

static void ts_wot_to_sleep_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ts_wot_l), 0);
}
DECLARE_DEFERRED(ts_wot_to_sleep_deferred);

static void ts_wot_sleep_mode(void)
{
	LOG_INF("WOT: Sleep Mode");

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en), 0);

	hook_call_deferred(&ts_wot_to_sleep_deferred_data, TS_WOT_DELAY_MS);
}

static void ts_wot_normal_mode(void)
{
	bool lid_open = lid_is_open();

	LOG_INF("WOT: Normal Mode, lid open? %d", lid_open);

	if (lid_open) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en),
				1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ts_wot_l), 0);
	}
}

static void wot_apply_state(bool tablet)
{
	if (tablet) {
		LOG_INF("WOT apply: WOT mode");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en),
				1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ts_wot_l), 1);
	} else {
		LOG_INF("WOT apply: Sleep mode");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en),
				0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ts_wot_l), 0);
	}
}

static void suspend_mode(void)
{
	bool cur_tablet = ts_get_tablet_mode();

	LOG_INF("WOT: Enter suspend, tablet=%d", cur_tablet);

	/* Apply correct mode once on suspend entry */
	wot_apply_state(cur_tablet);
}

static void wot_trigger_wake_deferred(void)
{
	LOG_INF("WOT: Deferred task executing power button press");
	power_button_simulate_press(PWR_BTN_DURATION);
}
DECLARE_DEFERRED(wot_trigger_wake_deferred);

void tchscr_event(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_detect)))
		return;

	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tchscr_int_l)))
		return;

	LOG_INF("WOT: Touch INT triggered (LOW), scheduling wake task");
	hook_call_deferred(&wot_trigger_wake_deferred_data, 0);
}

static void ts_wot_lid_change(void)
{
	bool lid_open = lid_is_open();

	/* Tablet mode does not need to go back to NORMAL mode */
	if (ts_get_tablet_mode())
		return;

	LOG_INF("WOT: lid change, open=%d", lid_open);

	if (!lid_open) {
		LOG_INF("WOT apply: lid close -> Sleep mode");
		ts_wot_sleep_mode();
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, ts_wot_lid_change, HOOK_PRIO_DEFAULT);

static void edp_backlight_detect_handler(void)
{
	int curr_backlight_on =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_detect));

	/* Init */
	if (prev_backlight_on == STATE_UNINITIALIZED) {
		prev_backlight_on = curr_backlight_on;
		return;
	}

	/* Only care if state changed */
	if (curr_backlight_on != prev_backlight_on) {
		if (!curr_backlight_on) {
			LOG_INF("Backlight OFF -> suspend_mode");
			suspend_mode();
		} else {
			LOG_INF("Backlight ON -> normal_mode");
			ts_wot_normal_mode();
		}

		prev_backlight_on = curr_backlight_on;
	}
}
DECLARE_HOOK(HOOK_TICK, edp_backlight_detect_handler, HOOK_PRIO_DEFAULT);
