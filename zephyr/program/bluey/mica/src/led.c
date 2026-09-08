/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mica lightbar LED control used to trigger a custom diagnostic sequence. */

#include "battery.h"
#include "hooks.h"
#include "lb_policy.h"
#include "led_common.h"
#include "led_lb_host_program.h"
#include "led_lightbar.h"
#include "util.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mica_led_diag, LOG_LEVEL_ERR);

/* true while power failure is detected. */
static bool power_fail_flags;

static void update_led_battery_vendor_fail_deferred(void);
DECLARE_DEFERRED(update_led_battery_vendor_fail_deferred);
static void update_led_battery_vendor_fail_deferred(void)
{
	lb_set_diag_policy(LED_ALT_POLICY_DIAG_BATTERY, 0);
}

/*
 * AP-triggered Diagnostics
 */
__override enum ec_status board_lightbar_custom_seq(uint8_t seq)
{
	switch (seq) {
	case LIGHTBAR_CMD_SEQ_RAMDUMP: /* Ramdump collection */
		lb_set_diag_policy(LED_ALT_POLICY_DIAG_RAMDUMP, 90000);
		return EC_RES_SUCCESS;

	case LIGHTBAR_CMD_SEQ_DIAG_CLEAR: /* Clear diagnostic state */
		lb_set_diag_policy(LED_ALT_POLICY_NORMAL, 0);
		return EC_RES_SUCCESS;

	case LIGHTBAR_CMD_SEQ_DIAG_LCD:
		/* Legacy behavior: LCD fail to diag 10 mins */
		lb_set_diag_policy(LED_ALT_POLICY_DIAG_LCD, 600000);
		LOG_ERR("LCD failed! LED Diag activated");
		return EC_RES_SUCCESS;

	default:
		return EC_RES_INVALID_PARAM;
	}
}

/*
 * EC-triggered Diagnostics
 */
void board_diag_led_power_fail(void)
{
	power_fail_flags = true;
	lb_set_diag_policy(LED_ALT_POLICY_DIAG_PWR, 600000);
	LOG_ERR("Power on failed! LED Diag activated");
}

/*
 * Clear the power fail diagnostic LED indication once AP
 * successfully reaches the startup state.
 *
 * This prevents the lightbar from remaining in a led diagnostic state
 * after AP has successfully completed the boot sequence.
 */
static void board_clear_diag_led(void)
{
	if (power_fail_flags == true) {
		power_fail_flags = false;
		lb_set_diag_policy(LED_ALT_POLICY_NORMAL, 0);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_clear_diag_led, HOOK_PRIO_DEFAULT);

/*
 * Check the battery vendor at chipset startup and activate LED diagnostics for
 * non-customer batteries.
 */
static void board_diag_led_battery_vendor_fail(void)
{
	struct battery_static_info *bs = &battery_static[BATT_IDX_MAIN];

	/* Check if the battery is a customer battery. */
	if (strncasecmp(bs->model_ext, "Dell", 4)) {
		LOG_ERR("No Dell battery! LED Diag activated");

		/* Deferred 5s to avoid state conflict */
		hook_call_deferred(
			&update_led_battery_vendor_fail_deferred_data,
			5 * USEC_PER_SEC);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_diag_led_battery_vendor_fail,
	     HOOK_PRIO_DEFAULT);
