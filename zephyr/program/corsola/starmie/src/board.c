/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "chipset.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

static void sense_startup_hook(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_STARTUP:
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_base_imu));
		break;
	case AP_POWER_SHUTDOWN:
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_base_imu));
		break;
	default:
		return;
	}
}
AP_POWER_EVENT_CALLBACK_DEFINE(sense_startup_hook, AP_POWER_STARTUP,
			       AP_POWER_SHUTDOWN);

static void sense_init(void)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_base_imu));
	}
}
DECLARE_HOOK(HOOK_INIT, sense_init, HOOK_PRIO_DEFAULT);
