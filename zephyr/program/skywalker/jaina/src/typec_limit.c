/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_pd.h"
#include "usbc/pdc_dpm.h"
#include "usbc/pdc_power_mgmt.h"
#include "util.h"

#include <zephyr/sys/atomic.h>

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* Threshold Specifications */
#define JAINA_MAX_DISCHARGING_CURRENT_MA 4500
#define JAINA_MAX_DISCHARGING_POWER_MW 41000

/* Bitmask to track which ports have been throttled */
static atomic_t overload_throttled_mask;

/**
 * Callback triggered upon Type-C device detachment.
 */
static void jaina_pdc_unattached_cb(int port)
{
	int i;
	pdc_dpm_clear_1500ma_lock(port);

	/* If a detachment occurs while any port is currently throttled */
	if (atomic_get(&overload_throttled_mask) != 0) {
		CPRINTS("Jaina Overload: Port %d removed, releasing ALL throttle locks.",
			port);

		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			if (atomic_test_bit(&overload_throttled_mask, i)) {
				/* Restore the 3.0A hardware current limit for
				 * other attached ports. */
				if (i != port) {
					pdc_dpm_clear_1500ma_lock(i);
					pdc_power_mgmt_set_current_limit(
						i, TC_CURRENT_3_0A);
					pdc_dpm_set_source_current_limit(i,
									 3000);
				}
			}
		}

		/* Clear all throttle locks to let the next HOOK_SECOND
		 * re-evaluate the system load. */
		atomic_set(&overload_throttled_mask, 0);
	}
}

static void jaina_overload_init(void)
{
	pdc_power_mgmt_register_board_callback(
		PDC_BOARD_CB_UNATTACH, (const void *)jaina_pdc_unattached_cb);
}
DECLARE_HOOK(HOOK_INIT, jaina_overload_init, HOOK_PRIO_DEFAULT);

/**
 * Monitor battery discharging status and throttle Type-C ports if needed.
 */
static void jaina_monitor_battery_overload(void)
{
	const struct batt_params *batt;
	int discharging_current_ma = 0;
	int discharging_power_mw = 0;
	int i;

	/* Skip protection and release locks if AC is attached */
	if (extpower_is_present()) {
		if (atomic_get(&overload_throttled_mask) != 0) {
			CPRINTS("Jaina Overload: AC attached, releasing all throttle locks.");

			for (i = 0; i < board_get_usb_pd_port_count(); i++) {
				if (atomic_test_bit(&overload_throttled_mask,
						    i)) {
					/* Restore hardware current limit to 3A
					 */
					pdc_power_mgmt_set_current_limit(
						i, TC_CURRENT_3_0A);
					/* Notify DPM to re-evaluate and sync
					 * state */
					pdc_dpm_set_source_current_limit(i,
									 3000);
				}
			}

			/* Clear all throttle locks */
			atomic_set(&overload_throttled_mask, 0);
		}
		return;
	}

	/* Retrieve cached battery parameters */
	batt = charger_current_battery_params();

	/* Abort if voltage or current data is marked as invalid */
	if (batt->flags & (BATT_FLAG_BAD_CURRENT | BATT_FLAG_BAD_VOLTAGE)) {
		return;
	}

	/* Filter out unexpected positive (charging) current */
	discharging_current_ma = MAX(0, -batt->current);
	discharging_power_mw = (discharging_current_ma * batt->voltage) / 1000;

	/* Check against spec limits and throttle if necessary */
	if ((discharging_current_ma > JAINA_MAX_DISCHARGING_CURRENT_MA) ||
	    (discharging_power_mw > JAINA_MAX_DISCHARGING_POWER_MW)) {
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			if ((pdc_dpm_get_source_current(i) == 3000) &&
			    !atomic_test_bit(&overload_throttled_mask, i)) {
				CPRINTS("Battery overload: Port %d throttled to 1.5A "
					"(I:%dmA, P:%dmW)",
					i, discharging_current_ma,
					discharging_power_mw);

				/* Force hardware current limit to 1.5A */
				pdc_power_mgmt_set_current_limit(
					i, TC_CURRENT_1_5A);

				pdc_dpm_set_source_current_limit(i, 1500);

				/* Record the throttled port to lock its state
				 */
				atomic_set_bit(&overload_throttled_mask, i);
			}
		}
	}
}
DECLARE_HOOK(HOOK_SECOND, jaina_monitor_battery_overload, HOOK_PRIO_DEFAULT);
