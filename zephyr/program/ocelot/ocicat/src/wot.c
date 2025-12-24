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

static int tablet_mode_override = -1;
/*
 * -1 : AUTO (use real tablet_mode)
 *  0 : FORCE clamshell
 *  1 : FORCE tablet
 */
static int last_tablet_mode = -1;

static int prev_backlight_on = -1; // init backlight state.

static void ts_sleep_to_wot_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en), 1);

	LOG_INF("WOT: WOT waiting 100ms set HIGH");
}
DECLARE_DEFERRED(ts_sleep_to_wot_deferred);

// static void ts_wot_mode(void)
// {
// 	LOG_INF("WOT: wot mode");

// 	/* Step 1: WOT LOW */
// 	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ts_wot_l), 1);

// 	/* Step 2 after 100ms */
// 	hook_call_deferred(&ts_sleep_to_wot_deferred_data, TS_WOT_DELAY_MS);
// }

static void ts_wot_to_sleep_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ts_wot_l), 0);

	LOG_INF("WOT: WOT waiting 100ms set LOW");
}
DECLARE_DEFERRED(ts_wot_to_sleep_deferred);

static void ts_wot_sleep_mode(void)
{
	LOG_INF("WOT: Sleep Mode");

	/* Step 1: REPORT_EN LOW */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en), 0);

	/* Step 2 after 100ms */
	hook_call_deferred(&ts_wot_to_sleep_deferred_data, TS_WOT_DELAY_MS);
}

static void ts_wot_normal_mode(void)
{
	bool lid_open = lid_is_open();

	LOG_INF("WOT: Normal Mode");
	LOG_INF("WOT: lid open?%d", lid_open);
	if (lid_open) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en),
				1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ts_wot_l), 0);
		last_tablet_mode = -1;
	}
}
// DECLARE_HOOK(HOOK_CHIPSET_RESUME_INIT, ts_wot_normal_mode,
// HOOK_PRIO_DEFAULT);

static inline bool ts_get_tablet_mode(void)
{
	/* Only controlled by EC console command */
	// return (tablet_mode_override == 1);
	return tablet_get_mode();
}

// static void ts_wot_apply_state(bool tablet)
// {
// 	if (tablet) {
// 		LOG_INF("WOT apply: tablet -> WOT mode");
// 		ts_wot_mode();
// 	} else {
// 		LOG_INF("WOT apply: clamshell -> Sleep mode");
// 		ts_wot_sleep_mode();
// 	}
// }

static void first_wot_apply_state(bool tablet)
{
	if (tablet) {
		LOG_INF("WOT first apply: WOT mode");

		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en),
				1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ts_wot_l), 1);
	} else {
		LOG_INF("WOT first apply: Sleep mode");

		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en),
				0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ts_wot_l), 0);
	}
}

static void suspend_mode(void)
{
	bool cur_tablet = ts_get_tablet_mode();

	LOG_INF("WOT: first enter suspend, tablet=%d", cur_tablet);

	/* Apply correct mode once on suspend entry */
	first_wot_apply_state(cur_tablet);

	/* Initialize change tracking */
	last_tablet_mode = cur_tablet;
}
// DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, suspend_mode, HOOK_PRIO_DEFAULT);

// // static void ts_wot_handle_change(bool cur_tablet)
// // {
// // 	/* First time */
// // 	if (last_tablet_mode == -1) {
// // 		last_tablet_mode = cur_tablet;
// // 		return;
// // 	}

// // 	if (cur_tablet == last_tablet_mode)
// // 		return;

// // 	LOG_INF("WOT change: tablet %d -> %d", last_tablet_mode, cur_tablet);

// // 	ts_wot_apply_state(cur_tablet);

// // 	last_tablet_mode = cur_tablet;
// // }

// static void ts_wot_sleep_controller(void)
// {
// 	/* Only operate during suspend */
// 	if (!(chipset_in_state(CHIPSET_STATE_STANDBY)))
// 		return;

// 	ts_wot_handle_change(ts_get_tablet_mode());
// }
// DECLARE_HOOK(HOOK_TICK, ts_wot_sleep_controller, HOOK_PRIO_DEFAULT);

static int command_tablet(int argc, const char **argv)
{
	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "on")) {
		tablet_mode_override = 1;
		ccprintf("Tablet mode forced ON\n");
	} else if (!strcasecmp(argv[1], "off")) {
		tablet_mode_override = 0;
		ccprintf("Tablet mode forced OFF\n");
	} else if (!strcasecmp(argv[1], "auto")) {
		tablet_mode_override = -1;
		ccprintf("Tablet mode AUTO\n");
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(wot, command_tablet, "on | off | auto",
			"Force tablet mode for WOT debug");

static void wot_trigger_wake_deferred(void)
{
	LOG_INF("WOT: Deferred task executing power button press");
	power_button_simulate_press(PWR_BTN_DURATION);
}
DECLARE_DEFERRED(wot_trigger_wake_deferred);

void tchscr_event(enum gpio_signal signal)
{
	LOG_INF("WOT: Ttchscr_event START");
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_detect)))
		return;

	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tchscr_int_l)))
		return;

	LOG_INF("WOT: Touch INT triggered (LOW), scheduling wake task");

	hook_call_deferred(&wot_trigger_wake_deferred_data, 0);
}

static void tchscr_int_l_handler(void)
{
	/* Only care when system is suspended */
	// if (!(chipset_in_state(CHIPSET_STATE_STANDBY)))
	// 	return;

	if ((gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_detect))))
		// LOG_INF("WOT: skip, BL OFF");
		return;

	if ((gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tchscr_int_l))))
		return;

	// LOG_INF("WOT: Touch INT asserted, send power button wake");
	LOG_INF("WOT: INT test, INT triggered....");

	/* Send power button wake event, Press duration 200ms*/
	// power_button_simulate_press(PWR_BTN_DURATION);
	// ap_power_ev_send_callbacks(AP_POWER_S0IX_RESUME);
}
DECLARE_HOOK(HOOK_TICK, tchscr_int_l_handler, HOOK_PRIO_DEFAULT);

static void ts_wot_lid_change(void)
{
	bool lid_open = lid_is_open();

	/* tablet mode not need back to NORMAL mode.*/
	if (ts_get_tablet_mode())
		return;

	LOG_INF("WOT: lid change, open=%d", lid_open);

	if (!lid_open) {
		/* Lid Closed -> Enter WOT sleep mode */
		LOG_INF("WOT apply: lid close -> Sleep mode");
		ts_wot_sleep_mode();
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, ts_wot_lid_change, HOOK_PRIO_DEFAULT);

static void edp_backlight_detect_handler(void)
{
	int curr_backlight_on;

	curr_backlight_on =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_detect));

	/* Init */
	if (prev_backlight_on == -1) {
		prev_backlight_on = curr_backlight_on;
		return;
	}

	/* Only Care state changed. */
	if (curr_backlight_on != prev_backlight_on) {
		if (!curr_backlight_on) {
			printk("Backlight OFF -> suspend_mode\n");
			suspend_mode();
		} else {
			printk("Backlight ON -> normal_mode\n");
			ts_wot_normal_mode();
		}

		prev_backlight_on = curr_backlight_on;
	}
}
DECLARE_HOOK(HOOK_TICK, edp_backlight_detect_handler, HOOK_PRIO_DEFAULT);
