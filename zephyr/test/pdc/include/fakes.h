/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_PDC_INCLUDE_FAKES_H_
#define PLATFORM_EC_ZEPHYR_TEST_PDC_INCLUDE_FAKES_H_

#include "battery.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, battery_design_voltage, int *);
DECLARE_FAKE_VALUE_FUNC(int, battery_remaining_capacity, int *);
DECLARE_FAKE_VALUE_FUNC(int, battery_status, int *);
DECLARE_FAKE_VALUE_FUNC(int, battery_design_capacity, int *);
DECLARE_FAKE_VALUE_FUNC(int, battery_full_charge_capacity, int *);

void set_battery_present(enum battery_present bp);

int battery_design_voltage_custom_fake(int *voltage);
int battery_remaining_capacity_custom_fake(int *capacity);
int battery_status_custom_fake(int *status);
int battery_design_capacity_custom_fake(int *design_capacity);
int battery_full_charge_capacity_custom_fake(int *full_charge_capacity);

void set_battery_design_voltage(int voltage);
void set_battery_remaining_capacity(int capacity);
void set_battery_status(int status);
void set_battery_design_capacity(int design_capacity);
void set_battery_full_charge_capacity(int full_charge_capacity);

extern int mock_usb_pd_3a_ports;

#endif /* PLATFORM_EC_ZEPHYR_TEST_PDC_INCLUDE_FAKES_H_ */
