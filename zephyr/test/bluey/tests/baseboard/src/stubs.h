/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BLUEY_BASEBOARD_TEST_STUBS_H
#define __BLUEY_BASEBOARD_TEST_STUBS_H

#include "adc.h"
#include "battery.h"
#include "chipset.h"
#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "throttle_ap.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
DECLARE_FAKE_VOID_FUNC(battery_poll_dynamic_info);
DECLARE_FAKE_VALUE_FUNC(int, update_static_battery_info);
DECLARE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DECLARE_FAKE_VALUE_FUNC(int, extpower_is_present);
DECLARE_FAKE_VALUE_FUNC(int, adc_read_channel, enum adc_channel);
DECLARE_FAKE_VOID_FUNC(throttle_ap_config_prochot, const struct prochot_cfg *);
DECLARE_FAKE_VOID_FUNC(throttle_ap_prochot_input_interrupt, enum gpio_signal);
DECLARE_FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);
DECLARE_FAKE_VOID_FUNC(chipset_reset, enum chipset_shutdown_reason);
DECLARE_FAKE_VOID_FUNC(chipset_power_on);
DECLARE_FAKE_VOID_FUNC(chipset_exit_hard_off);

#endif /* __BLUEY_BASEBOARD_TEST_STUBS_H */
