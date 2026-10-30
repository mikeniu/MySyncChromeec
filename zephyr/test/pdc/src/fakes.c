/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "fakes.h"

#include <zephyr/fff.h>

static enum battery_present bp_val = BP_YES;
void set_battery_present(enum battery_present bp)
{
	bp_val = bp;
}

int battery_is_present(void)
{
	return bp_val;
}

int test_extpower_present = 0;
int extpower_is_present(void)
{
	return test_extpower_present;
}

#ifndef CONFIG_PLATFORM_EC_USB_PD_3A_PORTS
#define CONFIG_PLATFORM_EC_USB_PD_3A_PORTS 0
#endif
int mock_usb_pd_3a_ports = CONFIG_PLATFORM_EC_USB_PD_3A_PORTS;
int pd_get_usb_pd_3a_ports(void)
{
	return mock_usb_pd_3a_ports;
}

__overridable const struct batt_params *charger_current_battery_params(void)
{
	static struct batt_params test_batt = {
		.voltage = 7700,
		.design_voltage = 7700,
		.design_capacity = 5000,
		.full_capacity = 4800,
		.remaining_capacity = 1000,
		.status = STATUS_DISCHARGING,
	};

	test_batt.is_present = battery_is_present();

	return &test_batt;
}
