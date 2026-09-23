/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "stubs.h"

DEFINE_FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
DEFINE_FAKE_VOID_FUNC(battery_poll_dynamic_info);
DEFINE_FAKE_VALUE_FUNC(int, update_static_battery_info);
DEFINE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DEFINE_FAKE_VALUE_FUNC(int, extpower_is_present);
DEFINE_FAKE_VALUE_FUNC(int, adc_read_channel, enum adc_channel);
DEFINE_FAKE_VOID_FUNC(throttle_ap_config_prochot, const struct prochot_cfg *);
DEFINE_FAKE_VOID_FUNC(throttle_ap_prochot_input_interrupt, enum gpio_signal);
DEFINE_FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);
DEFINE_FAKE_VOID_FUNC(chipset_reset, enum chipset_shutdown_reason);
DEFINE_FAKE_VOID_FUNC(chipset_power_on);
DEFINE_FAKE_VOID_FUNC(chipset_exit_hard_off);
