/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "hooks.h"
#include "usb_pd.h"

#define G3_VBUS_LIMIT_MV 5000
// #define PD_MAX_VOLTAGE_MV 20000

static void limit_vbus(void)
{
	int port;
	const struct batt_params *batt = charger_current_battery_params();

	if ((batt->current == 0) && (batt->status & STATUS_FULLY_CHARGED) &&
	    (pd_get_max_voltage() != G3_VBUS_LIMIT_MV)) {
		port = charge_manager_get_active_charge_port();
		pd_request_source_voltage(port, G3_VBUS_LIMIT_MV);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, limit_vbus, HOOK_PRIO_DEFAULT);

static void unlimit_vbus(void)
{
	int port;
	if (pd_get_max_voltage() != PD_MAX_VOLTAGE_MV) {
		port = charge_manager_get_active_charge_port();
		pd_request_source_voltage(port, PD_MAX_VOLTAGE_MV);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, unlimit_vbus, HOOK_PRIO_POST_DEFAULT);
