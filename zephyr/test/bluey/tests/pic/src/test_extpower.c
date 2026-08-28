/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "extpower.h"
#include "gpio.h"
#include "stubs.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static void test_extpower_before(void *fixture)
{
	ARG_UNUSED(fixture);

	const struct gpio_dt_spec *pgood =
		GPIO_DT_FROM_NODELABEL(gpio_pgood_acok_odl);

	gpio_emul_input_set(pgood->port, pgood->pin, 1);
}

ZTEST_SUITE(pic_extpower, NULL, NULL, test_extpower_before, NULL, NULL);

ZTEST(pic_extpower, test_is_present)
{
	const struct gpio_dt_spec *pgood =
		GPIO_DT_FROM_NODELABEL(gpio_pgood_acok_odl);

	/* Physical high on active-low pin -> logical 0 */
	gpio_emul_input_set(pgood->port, pgood->pin, 1);
	zassert_equal(board_extpower_is_present(), 0);

	/* Physical low on active-low pin -> logical 1 */
	gpio_emul_input_set(pgood->port, pgood->pin, 0);
	zassert_equal(board_extpower_is_present(), 1);
}

ZTEST(pic_extpower, test_interrupts)
{
	board_extpower_enable_interrupt();
	board_extpower_disable_interrupt();
}
