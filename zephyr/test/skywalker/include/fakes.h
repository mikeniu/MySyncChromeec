/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);
DECLARE_FAKE_VOID_FUNC(bma4xx_interrupt, enum gpio_signal);
DECLARE_FAKE_VOID_FUNC(lis2dw12_interrupt, enum gpio_signal);
DECLARE_FAKE_VALUE_FUNC(int, pd_check_vconn_swap, int);
DECLARE_FAKE_VALUE_FUNC(int, pd_set_power_supply_ready, int);
DECLARE_FAKE_VOID_FUNC(pd_power_supply_reset, int);
DECLARE_FAKE_VOID_FUNC(xhci_interrupt, enum gpio_signal);
DECLARE_FAKE_VALUE_FUNC(int, board_is_convertible);
DECLARE_FAKE_VALUE_FUNC(int, board_sensor_at_base, int);
DECLARE_FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
