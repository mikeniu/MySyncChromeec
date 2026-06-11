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

/* Track if the system is in a battery overload state.
 * 0: Normal operation (3A allowed)
 * 1: Overload state (3A budget forced to 0)
 */
static atomic_t is_battery_overloaded = ATOMIC_INIT(0);

/* Override the default 3A port allocation.
 * This serves as the single source of truth for the DPM's 3A budget.
 */
__override int pd_get_usb_pd_3a_ports(void)
{
	/* Block all 3A allocations if the system is currently overloaded */
	if (atomic_get(&is_battery_overloaded) != 0) {
		return 0;
	}

	/* Normal operation: return the system's default 3A budget */
	return CONFIG_USB_PD_3A_PORTS;
}

/* Callback triggered upon Type-C device detachment */
static void jaina_pdc_unattached_cb(int port)
{
	int i;

	/* If a detachment occurs while overloaded, release the lock.
	 * atomic_cas ensures this block runs exactly once during the
	 * transition.
	 */
	if (atomic_cas(&is_battery_overloaded, 1, 0)) {
		CPRINTS("Jaina Overload: Port %d removed, restoring 3A capacity.",
			port);

		/* Notify all ports to re-evaluate their PD contracts with the
		 * restored budget */
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			if (i != port) {
				pdc_power_mgmt_set_current_limit(
					i, TC_CURRENT_3_0A);
				pdc_dpm_set_source_current_limit(i, 3000);
				pdc_power_mgmt_set_new_power_request(i);
			}
		}
	}
}

static void jaina_overload_init(void)
{
	pdc_power_mgmt_register_board_callback(
		PDC_BOARD_CB_UNATTACH, (const void *)jaina_pdc_unattached_cb);
}
DECLARE_HOOK(HOOK_INIT, jaina_overload_init, HOOK_PRIO_DEFAULT);

/* Monitor battery discharging status and throttle Type-C ports if needed */
static void jaina_monitor_battery_overload(void)
{
	const struct batt_params *batt;
	int discharging_current_ma = 0;
	int discharging_power_mw = 0;
	int i;

	/* Skip protection logic if AC is attached, but DO NOT release locks */
	if (extpower_is_present()) {
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
		/* Transition to overload state.
		 * atomic_cas ensures we only trigger the DPM re-evaluation once
		 * per overload event.
		 */
		if (atomic_cas(&is_battery_overloaded, 0, 1)) {
			CPRINTS("Battery overload: Throttling all ports to 1.5A "
				"(I:%dmA, P:%dmW)",
				discharging_current_ma, discharging_power_mw);

			/* Notify all ports to re-evaluate their PD contracts
			 * with 0 budget */
			for (i = 0; i < board_get_usb_pd_port_count(); i++) {
				pdc_power_mgmt_set_current_limit(
					i, TC_CURRENT_1_5A);
				pdc_power_mgmt_set_new_power_request(i);
			}
		}
	}
}
DECLARE_HOOK(HOOK_SECOND, jaina_monitor_battery_overload, HOOK_PRIO_DEFAULT);
