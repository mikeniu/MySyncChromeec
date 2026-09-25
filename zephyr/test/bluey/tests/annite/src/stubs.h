/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_BLUEY_TESTS_ANNITE_SRC_STUBS_H_
#define PLATFORM_EC_ZEPHYR_TEST_BLUEY_TESTS_ANNITE_SRC_STUBS_H_

#include "adc.h"
#include "battery.h"
#include "chipset.h"
#include "common.h"
#include "fan.h"
#include "gpio.h"
#include "host_command.h"
#include "power/qcom.h"
#include "temp_sensor/temp_sensor.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(enum power_on_event_t, chipset_get_power_on_reason);
DECLARE_FAKE_VOID_FUNC(fan_set_duty, int, int);
DECLARE_FAKE_VALUE_FUNC(enum fan_status, fan_smart_control, int);
DECLARE_FAKE_VOID_FUNC(fan_set_rpm_mode, int, int);
DECLARE_FAKE_VOID_FUNC(fan_set_rpm_target, int, int);
DECLARE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DECLARE_FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);
DECLARE_FAKE_VALUE_FUNC(int, adc_read_channel, enum adc_channel);
DECLARE_FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
DECLARE_FAKE_VOID_FUNC(host_set_single_event, enum host_event_code);
DECLARE_FAKE_VOID_FUNC(extpower_interrupt, enum gpio_signal);

#endif /* PLATFORM_EC_ZEPHYR_TEST_BLUEY_TESTS_ANNITE_SRC_STUBS_H_ */
