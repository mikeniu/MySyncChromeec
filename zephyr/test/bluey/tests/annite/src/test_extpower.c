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

ZTEST_SUITE(annite_extpower, NULL, NULL, test_extpower_before, NULL, NULL);

/* Test external power detection */
ZTEST(annite_extpower, test_is_present)
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

/* Test external power interrupt enable and disable */
ZTEST(annite_extpower, test_interrupts)
{
	const struct gpio_dt_spec *pgood =
		GPIO_DT_FROM_NODELABEL(gpio_pgood_acok_odl);
	gpio_flags_t flags;

	board_extpower_enable_interrupt();
	zassert_ok(gpio_emul_flags_get(pgood->port, pgood->pin, &flags));
	zassert_true((flags & GPIO_INT_ENABLE) != 0,
		     "Interrupt should be enabled on PGOOD ACOK");

	board_extpower_disable_interrupt();
	zassert_ok(gpio_emul_flags_get(pgood->port, pgood->pin, &flags));
	zassert_true((flags & GPIO_INT_DISABLE) != 0,
		     "Interrupt should be disabled on PGOOD ACOK");
}
