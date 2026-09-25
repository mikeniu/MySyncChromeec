/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "extpower.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

static bool is_early_board(void)
{
	uint32_t board_id;

	if (cbi_get_board_version(&board_id) != EC_SUCCESS) {
		board_id = 0;
	}

	return board_id <= 1;
}

int board_extpower_is_present(void)
{
	if (is_early_board()) {
		/* The pins are ACTIVE_LOW in DTS, so gpio_pin_get_dt returns 1
		 * when connected. */
		return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
			       gpio_smb2360_a_chg_led_pg_odl)) ||
		       gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
			       gpio_smb2360_b_chg_led_pg_odl));
	} else {
		return gpio_pin_get_dt(
			GPIO_DT_FROM_NODELABEL(gpio_pgood_acok_odl));
	}
}

void board_extpower_enable_interrupt(void)
{
	if (is_early_board()) {
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_extpower_a));
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_extpower_b));
	} else {
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_gpio_pgood_acok_odl));
	}
}

void board_extpower_disable_interrupt(void)
{
	if (is_early_board()) {
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_extpower_a));
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_extpower_b));
	} else {
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_gpio_pgood_acok_odl));
	}
}
