/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "fakes.h"

#include <zephyr/fff.h>

DEFINE_FAKE_VALUE_FUNC(int, battery_design_voltage, int *);
DEFINE_FAKE_VALUE_FUNC(int, battery_remaining_capacity, int *);
DEFINE_FAKE_VALUE_FUNC(int, battery_status, int *);
DEFINE_FAKE_VALUE_FUNC(int, battery_design_capacity, int *);
DEFINE_FAKE_VALUE_FUNC(int, battery_full_charge_capacity, int *);

static int design_voltage_val = 0;
void set_battery_design_voltage(int voltage)
{
	design_voltage_val = voltage;
}
int battery_design_voltage_custom_fake(int *voltage)
{
	*voltage = design_voltage_val;
	return battery_design_voltage_fake.return_val;
}

static int remaining_capacity_val = 0;
void set_battery_remaining_capacity(int capacity)
{
	remaining_capacity_val = capacity;
}
int battery_remaining_capacity_custom_fake(int *capacity)
{
	*capacity = remaining_capacity_val;
	return battery_remaining_capacity_fake.return_val;
}

static int battery_status_val = 0;
void set_battery_status(int status)
{
	battery_status_val = status;
}
int battery_status_custom_fake(int *status)
{
	*status = battery_status_val;
	return battery_status_fake.return_val;
}

static int design_capacity_val = 0;
void set_battery_design_capacity(int design_capacity)
{
	design_capacity_val = design_capacity;
}
int battery_design_capacity_custom_fake(int *design_capacity)
{
	*design_capacity = design_capacity_val;
	return battery_design_capacity_fake.return_val;
}

static int full_charge_capacity_val = 0;
void set_battery_full_charge_capacity(int full_charge_capacity)
{
	full_charge_capacity_val = full_charge_capacity;
}
int battery_full_charge_capacity_custom_fake(int *c)
{
	*c = full_charge_capacity_val;
	return battery_full_charge_capacity_fake.return_val;
}

static enum battery_present bp_val = BP_YES;
void set_battery_present(enum battery_present bp)
{
	bp_val = bp;
}

int battery_is_present(void)
{
	return bp_val;
}

DEFINE_FAKE_VALUE_FUNC(int, system_is_locked);
DEFINE_FAKE_VALUE_FUNC(int, board_allow_i2c_passthru,
		       const struct i2c_cmd_desc_t *);

int extpower_is_present(void)
{
	return 0;
}
