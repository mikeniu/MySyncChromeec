/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fakes.h"

DEFINE_FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);
DEFINE_FAKE_VOID_FUNC(bma4xx_interrupt, enum gpio_signal);
DEFINE_FAKE_VOID_FUNC(lis2dw12_interrupt, enum gpio_signal);
DEFINE_FAKE_VALUE_FUNC(int, pd_check_vconn_swap, int);
DEFINE_FAKE_VALUE_FUNC(int, pd_set_power_supply_ready, int);
DEFINE_FAKE_VOID_FUNC(pd_power_supply_reset, int);
DEFINE_FAKE_VOID_FUNC(xhci_interrupt, enum gpio_signal);
DEFINE_FAKE_VALUE_FUNC(int, board_is_convertible);
DEFINE_FAKE_VALUE_FUNC(int, board_sensor_at_base, int);
DEFINE_FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
