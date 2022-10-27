/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "button_config.h"
#include "hooks.h"
#include "host_command.h"
#include "include/button.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "task.h"

LOG_MODULE_REGISTER(power_button, LOG_LEVEL_ERR);

static int debounced_power_pressed; /* Debounced power button state */
static int simulate_power_pressed;
static volatile int power_button_is_stable = 1;

int power_button_signal_asserted(void)
{
	const struct button_config_v2 *cfg =
		button_cfg_get(BUTTON_CFG_POWER_BUTTON);
	const int is_active_high =
		((cfg->button_flags & BUTTON_FLAG_ACTIVE_HIGH) ? 1 : 0);

	return button_is_pressed_raw(BUTTON_CFG_POWER_BUTTON) == is_active_high;
}

static int raw_power_button_pressed(void)
{
	if (simulate_power_pressed)
		return 1;

#ifndef CONFIG_POWER_BUTTON_IGNORE_LID
	/*
	 * Always indicate power button released if the lid is closed.
	 * This prevents waking the system if the device is squashed enough to
	 * press the power button through the closed lid.
	 */
	if (!lid_is_open())
		return 0;
#endif

	return power_button_signal_asserted();
}

int power_button_is_pressed(void)
{
	return debounced_power_pressed;
}

int power_button_wait_for_release(int timeout_us)
{
	timestamp_t deadline;
	timestamp_t now = get_time();

	deadline.val = now.val + timeout_us;

	while (!power_button_is_stable || power_button_is_pressed()) {
		now = get_time();
		if (timeout_us >= 0 && timestamp_expired(deadline, &now)) {
			LOG_INF("%s not released in time",
				button_get_name(BUTTON_CFG_POWER_BUTTON));
			return EC_ERROR_TIMEOUT;
		}
		/*
		 * We use task_wait_event() instead of usleep() here. It will
		 * be woken up immediately if the power button is debouned and
		 * changed. However, it is not guaranteed, like the cases that
		 * the power button is debounced but not changed, or the power
		 * button has not been debounced.
		 */
		task_wait_event(
			MIN(button_get_debounce_us(BUTTON_CFG_POWER_BUTTON),
			    deadline.val - now.val));
	}

	LOG_INF("%s released in time",
		button_get_name(BUTTON_CFG_POWER_BUTTON));
	return EC_SUCCESS;
}

/**
 * Handle power button initialization.
 */
static void power_button_init(void)
{
	if (raw_power_button_pressed())
		debounced_power_pressed = 1;

	/* Enable interrupts, now that we've initialized */
	button_enable_interrupt(BUTTON_CFG_POWER_BUTTON);
}

DECLARE_HOOK(HOOK_INIT, power_button_init, HOOK_PRIO_INIT_POWER_BUTTON);

/**
 * Handle debounced power button changing state.
 */
static void power_button_change_deferred(void)
{
	const int new_pressed = raw_power_button_pressed();

	/* Re-enable keyboard scanning if power button is no longer pressed */
	if (!new_pressed)
		keyboard_scan_enable(1, KB_SCAN_DISABLE_POWER_BUTTON);

	/* If power button hasn't changed state, nothing to do */
	if (new_pressed == debounced_power_pressed) {
		power_button_is_stable = 1;
		return;
	}

	debounced_power_pressed = new_pressed;
	power_button_is_stable = 1;

	LOG_INF("%s %s", button_get_name(BUTTON_CFG_POWER_BUTTON),
		new_pressed ? "pressed" : "released");

	/* Call hooks */
	hook_notify(HOOK_POWER_BUTTON_CHANGE);

	/* Notify host if power button has been pressed */
	if (new_pressed)
		host_set_single_event(EC_HOST_EVENT_POWER_BUTTON);
}
DECLARE_DEFERRED(power_button_change_deferred);

void power_button_interrupt(enum gpio_signal signal)
{
	ARG_UNUSED(signal);
	/*
	 * If power button is pressed, disable the matrix scan as soon as
	 * possible to reduce the risk of false-reboot triggered by those keys
	 * on the same column with refresh key.
	 */
	if (raw_power_button_pressed())
		keyboard_scan_enable(0, KB_SCAN_DISABLE_POWER_BUTTON);

	/* Reset power button debounce time */
	power_button_is_stable = 0;
	hook_call_deferred(&power_button_change_deferred_data,
			   button_get_debounce_us(BUTTON_CFG_POWER_BUTTON));
}

void power_button_simulate_press(unsigned int duration)
{
	LOG_INF("Simulating %d ms %s press.\n", duration,
		button_get_name(BUTTON_CFG_POWER_BUTTON));
	simulate_power_pressed = 1;
	power_button_is_stable = 0;
	hook_call_deferred(&power_button_change_deferred_data, 0);

	if (duration > 0)
		msleep(duration);

	LOG_INF("Simulating %s release.\n",
		button_get_name(BUTTON_CFG_POWER_BUTTON));
	simulate_power_pressed = 0;
	power_button_is_stable = 0;
	hook_call_deferred(&power_button_change_deferred_data, 0);
}

/*****************************************************************************/
/* Console commands */

static int command_powerbtn(int argc, const char **argv)
{
	int ms = 200; /* Press duration in ms */
	char *e;

	if (argc > 1) {
		ms = strtoi(argv[1], &e, 0);
		if (*e || ms < 0)
			return EC_ERROR_PARAM1;
	}

	power_button_simulate_press(ms);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtn, command_powerbtn, "[msec]",
			"Simulate power button press");
