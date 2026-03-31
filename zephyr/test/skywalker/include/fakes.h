/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_SKYWALKER_INCLUDE_FAKES_H_
#define ZEPHYR_TEST_SKYWALKER_INCLUDE_FAKES_H_

#include "battery.h"
#include "gpio.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);
DECLARE_FAKE_VOID_FUNC(bma4xx_interrupt, enum gpio_signal);
DECLARE_FAKE_VOID_FUNC(lis2dw12_interrupt, enum gpio_signal);
DECLARE_FAKE_VOID_FUNC(xhci_interrupt, enum gpio_signal);
DECLARE_FAKE_VALUE_FUNC(int, pd_check_vconn_swap, int);
DECLARE_FAKE_VALUE_FUNC(int, pd_set_power_supply_ready, int);
DECLARE_FAKE_VOID_FUNC(pd_power_supply_reset, int);

#endif /* ZEPHYR_TEST_SKYWALKER_INCLUDE_FAKES_H_ */
