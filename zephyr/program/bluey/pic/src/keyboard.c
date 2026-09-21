/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "timer.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pic_keyboard, LOG_LEVEL_INF);

#define CROS_EC_KEYBOARD_NODE DT_CHOSEN(cros_ec_keyboard)

/*
 * Dedicated GPIOB5 screen-lock button, physically to the right of F14.
 * Report it as C9R3 so the AP keymap treats it as KEY_SLEEP / lock.
 */
#define SCRNLOCK_ROW 3
#define SCRNLOCK_COL 9
#define SCRNLOCK_DEBOUNCE_US (20 * MSEC)

static const struct device *const kbd_dev =
	DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE);

static int last_pressed = -1;

static void report_scrnlock(int pressed)
{
	input_report_abs(kbd_dev, INPUT_ABS_X, SCRNLOCK_COL, false, K_FOREVER);
	input_report_abs(kbd_dev, INPUT_ABS_Y, SCRNLOCK_ROW, false, K_FOREVER);
	input_report_key(kbd_dev, INPUT_BTN_TOUCH, pressed, true, K_FOREVER);
}

static void scrnlock_deferred(void)
{
	int pressed =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_scrnlock_l));

	if (pressed < 0 || pressed == last_pressed)
		return;

	last_pressed = pressed;
	report_scrnlock(pressed);
	LOG_DBG("scrnlock %s", pressed ? "pressed" : "released");
}
DECLARE_DEFERRED(scrnlock_deferred);

void scrnlock_interrupt(enum gpio_signal signal)
{
	ARG_UNUSED(signal);
	hook_call_deferred(&scrnlock_deferred_data, SCRNLOCK_DEBOUNCE_US);
}

static void scrnlock_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_scrnlock));
	last_pressed =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_scrnlock_l));
	if (last_pressed > 0)
		report_scrnlock(1);
}
DECLARE_HOOK(HOOK_INIT, scrnlock_init, HOOK_PRIO_DEFAULT);
