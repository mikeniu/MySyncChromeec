/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __QUENBI_BOARD_TEST_STUBS_H
#define __QUENBI_BOARD_TEST_STUBS_H

#include "chipset.h"
#include "common.h"
#include "fan.h"
#include "power/qcom.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(enum power_on_event_t, chipset_get_power_on_reason);
DECLARE_FAKE_VOID_FUNC(fan_set_duty, int, int);
DECLARE_FAKE_VALUE_FUNC(int, fan_get_rpm_actual, int);
DECLARE_FAKE_VOID_FUNC(fan_set_rpm_mode, int, int);
DECLARE_FAKE_VOID_FUNC(fan_set_rpm_target, int, int);
DECLARE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DECLARE_FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);
DECLARE_FAKE_VOID_FUNC(chipset_reset, enum chipset_shutdown_reason);
DECLARE_FAKE_VOID_FUNC(chipset_power_on);
DECLARE_FAKE_VOID_FUNC(chipset_exit_hard_off);

#endif /* __QUENBI_BOARD_TEST_STUBS_H */
