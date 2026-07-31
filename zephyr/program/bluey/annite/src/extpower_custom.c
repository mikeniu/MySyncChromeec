/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Annite extpower configuration */

#include "extpower.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

int board_extpower_is_present(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pgood_acok_odl));
}

void board_extpower_enable_interrupt(void)
{
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_gpio_pgood_acok_odl));
}

void board_extpower_disable_interrupt(void)
{
	gpio_disable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_gpio_pgood_acok_odl));
}
