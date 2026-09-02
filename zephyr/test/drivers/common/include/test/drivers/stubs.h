/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_DRIVERS_COMMON_INCLUDE_TEST_DRIVERS_STUBS_H_
#define PLATFORM_EC_ZEPHYR_TEST_DRIVERS_COMMON_INCLUDE_TEST_DRIVERS_STUBS_H_

#include "power.h"

#include <zephyr/fff.h>

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };

/**
 * @brief Set product ID that should be returned by board_get_ps8xxx_product_id
 *
 * @param product_id ID of PS8xxx product which is emulated
 */
void board_set_ps8xxx_product_id(uint16_t product_id);

/* Declare fake function to allow tests to examine calls to this function */
DECLARE_FAKE_VOID_FUNC(system_hibernate, uint32_t, uint32_t);

DECLARE_FAKE_VOID_FUNC(board_reset_pd_mcu);

void sys_arch_reboot(int type);

/* Declare GPIO_TEST interrupt handler */
void gpio_test_interrupt(enum gpio_signal signal);
#endif /* PLATFORM_EC_ZEPHYR_TEST_DRIVERS_COMMON_INCLUDE_TEST_DRIVERS_STUBS_H_ \
	*/
