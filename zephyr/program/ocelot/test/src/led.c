/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "hooks.h"
#include "led_common.h"
#include "lid_switch.h"

#include <ap_power/ap_power.h>
#include <drivers/led.h>

/*
 * HP LED spec 2.1, Power/Standby LED:
 * "The blinking/Breathing should be off when the lid is closed."
 *
 * The lid state cannot be fed into the devicetree policy through
 * board_led_alt_policy(): that symbol is already provided by the Google
 * Private lightbar module (CONFIG_PLATFORM_EC_LED_DT_LIGHTBAR), which this
 * board needs for its RGB strip. Gate the automatic policy from board code
 * instead: while the lid is closed in suspend, take the power LED away from
 * the devicetree policy and keep it off; hand it back as soon as the lid is
 * opened or the AP leaves suspend (S0 is solid blue either way).
 */
static bool power_led_forced_off;

static void power_led_set_forced_off(bool force_off)
{
	if (force_off == power_led_forced_off) {
		return;
	}

	power_led_forced_off = force_off;

	if (force_off) {
		/* Stop the devicetree policy and blank the LED */
		led_auto_control(EC_LED_ID_POWER_LED, 0);
		led_set_color(LED_OFF, EC_LED_ID_POWER_LED, 100);
	} else {
		/* Re-enabling auto control restores the devicetree policy */
		led_auto_control(EC_LED_ID_POWER_LED, 1);
	}
}

static void power_led_lid_change(void)
{
	power_led_set_forced_off(!lid_is_open() &&
				 chipset_in_state(CHIPSET_STATE_ANY_SUSPEND));
}
DECLARE_HOOK(HOOK_LID_CHANGE, power_led_lid_change, HOOK_PRIO_DEFAULT);

static void power_led_power_event(struct ap_power_ev_callback *callback,
				  struct ap_power_ev_data data)
{
	ARG_UNUSED(callback);

	switch (data.event) {
	case AP_POWER_S0IX_SUSPEND:
		/* S3/S0ix: breathing only while the lid is open */
		power_led_set_forced_off(!lid_is_open());
		break;
	case AP_POWER_S0IX_RESUME:
		/* Back to S0: solid blue, lid state does not matter */
		power_led_set_forced_off(false);
		break;
	default:
		break;
	}
}
AP_POWER_EVENT_CALLBACK_DEFINE(power_led_power_event, AP_POWER_S0IX_SUSPEND,
			       AP_POWER_S0IX_RESUME);
