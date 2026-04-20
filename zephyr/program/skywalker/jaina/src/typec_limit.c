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

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* Threshold Specifications */
#define JAINA_MAX_DISCHARGING_CURRENT_MA 4500
#define JAINA_MAX_DISCHARGING_POWER_MW 41000

/* Bitmask to track which ports have been throttled */
static uint8_t overload_throttled_mask = 0;

/**
 * Callback triggered upon Type-C device detachment.
 */
static void jaina_pdc_unattached_cb(int port)
{
	int i;

	/* If a detachment occurs while any port is currently throttled */
	if (overload_throttled_mask != 0) {
		CPRINTS("Jaina Overload: Port %d removed, releasing ALL throttle locks.",
			port);

		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			if (overload_throttled_mask & BIT(i)) {
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
		overload_throttled_mask = 0;
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
		if (overload_throttled_mask != 0) {
			CPRINTS("Jaina Overload: AC attached, releasing all throttle locks.");

			for (i = 0; i < board_get_usb_pd_port_count(); i++) {
				if (overload_throttled_mask & BIT(i)) {
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
			overload_throttled_mask = 0;
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
			    !(overload_throttled_mask & BIT(i))) {
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
				overload_throttled_mask |= BIT(i);
			}
		}
	}
}
DECLARE_HOOK(HOOK_SECOND, jaina_monitor_battery_overload, HOOK_PRIO_DEFAULT);
#include "usbc/pdc_power_mgmt.h"

#include <stdlib.h>

#include <zephyr/shell/shell.h>

static int cmd_jaina_tc_current(const struct shell *sh, size_t argc,
				char **argv)
{
	int port;
	int ret;
	union connector_status_t conn_status;

	if (argc < 2) {
		shell_error(sh, "Usage: jaina_tc_current <port_number>");
		return -EINVAL;
	}

	port = strtol(argv[1], NULL, 10);

	ret = pdc_power_mgmt_get_connector_status(port, &conn_status);
	if (ret != 0) {
		shell_error(sh,
			    "Failed to read PDC status for port %d (Error: %d)",
			    port, ret);
		return ret;
	}

	shell_print(sh, "Port %d Power Operation Mode: %d", port,
		    conn_status.power_operation_mode);

	switch ((int)conn_status.power_operation_mode) {
	case 0:
		shell_print(sh, " -> [0] No Consumer");
		break;
	case 1:
		shell_print(
			sh,
			" -> [1] USB Default Operation / Type-C Current default");
		break;
	case 2:
		shell_print(sh, " -> [2] BC (Battery Charging)");
		break;
	case 3:
		shell_print(sh, " -> [3] PD Contract");
		break;
	case 4:
		shell_print(sh, " -> [4] Type-C Current - 1.5A");
		break;
	case 5:
		shell_print(sh, " -> [5] Type-C Current - 3.0A");
		break;
	default:
		shell_print(sh, " -> [%d] Reserved/Unknown",
			    (int)conn_status.power_operation_mode);
		break;
	}

	return 0;
}
SHELL_CMD_REGISTER(jaina_tc_current, NULL, "Get Type-C current mode from PDC",
		   cmd_jaina_tc_current);
