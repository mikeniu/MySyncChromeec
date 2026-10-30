/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_AP_POWER_INCLUDE_TEST_MOCKS_H_
#define PLATFORM_EC_ZEPHYR_TEST_AP_POWER_INCLUDE_TEST_MOCKS_H_

#include "gpio_signal.h"

#include <stdint.h>

#include <zephyr/fff.h>

/*
 * Mock declarations
 */

/* Mocks for common/extpower_gpio.c */
DECLARE_FAKE_VALUE_FUNC(int, extpower_is_present);
DECLARE_FAKE_VOID_FUNC(extpower_interrupt, enum gpio_signal);

/* Mocks for common/system.c */
DECLARE_FAKE_VOID_FUNC(system_hibernate, uint32_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int, system_can_boot_ap);

#endif /* PLATFORM_EC_ZEPHYR_TEST_AP_POWER_INCLUDE_TEST_MOCKS_H_ */
