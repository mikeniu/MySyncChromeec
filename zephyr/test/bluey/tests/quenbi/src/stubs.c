/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "stubs.h"

DEFINE_FAKE_VALUE_FUNC(enum power_on_event_t, chipset_get_power_on_reason);
DEFINE_FAKE_VOID_FUNC(fan_set_duty, int, int);
DEFINE_FAKE_VALUE_FUNC(int, fan_get_rpm_actual, int);
DEFINE_FAKE_VOID_FUNC(fan_set_rpm_mode, int, int);
DEFINE_FAKE_VOID_FUNC(fan_set_rpm_target, int, int);
DEFINE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DEFINE_FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);
DEFINE_FAKE_VOID_FUNC(chipset_reset, enum chipset_shutdown_reason);
DEFINE_FAKE_VOID_FUNC(chipset_power_on);
DEFINE_FAKE_VOID_FUNC(chipset_exit_hard_off);
