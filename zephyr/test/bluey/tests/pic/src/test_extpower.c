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

	const struct gpio_dt_spec *a =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_a_chg_led_pg_odl);
	const struct gpio_dt_spec *b =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_b_chg_led_pg_odl);

	gpio_emul_input_set(a->port, a->pin, 1);
	gpio_emul_input_set(b->port, b->pin, 1);
}

ZTEST_SUITE(pic_extpower, NULL, NULL, test_extpower_before, NULL, NULL);

/* Test external power detection */
ZTEST(pic_extpower, test_is_present)
{
	const struct gpio_dt_spec *a =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_a_chg_led_pg_odl);
	const struct gpio_dt_spec *b =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_b_chg_led_pg_odl);

	/* Both high on active-low pin -> logical 0 */
	gpio_emul_input_set(a->port, a->pin, 1);
	gpio_emul_input_set(b->port, b->pin, 1);
	zassert_equal(board_extpower_is_present(), 0);

	/* A low on active-low pin -> logical 1 */
	gpio_emul_input_set(a->port, a->pin, 0);
	zassert_equal(board_extpower_is_present(), 1);

	/* B low on active-low pin -> logical 1 */
	gpio_emul_input_set(a->port, a->pin, 1);
	gpio_emul_input_set(b->port, b->pin, 0);
	zassert_equal(board_extpower_is_present(), 1);

	/* Both low on active-low pin -> logical 1 */
	gpio_emul_input_set(a->port, a->pin, 0);
	zassert_equal(board_extpower_is_present(), 1);
}

/* Test external power interrupt enable and disable */
ZTEST(pic_extpower, test_interrupts)
{
	const struct gpio_dt_spec *a =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_a_chg_led_pg_odl);
	const struct gpio_dt_spec *b =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_b_chg_led_pg_odl);
	gpio_flags_t flags_a, flags_b;

	board_extpower_enable_interrupt();

	zassert_ok(gpio_emul_flags_get(a->port, a->pin, &flags_a));
	zassert_true((flags_a & GPIO_INT_ENABLE) != 0,
		     "Interrupt should be enabled on port A");

	zassert_ok(gpio_emul_flags_get(b->port, b->pin, &flags_b));
	zassert_true((flags_b & GPIO_INT_ENABLE) != 0,
		     "Interrupt should be enabled on port B");

	board_extpower_disable_interrupt();

	zassert_ok(gpio_emul_flags_get(a->port, a->pin, &flags_a));
	zassert_true((flags_a & GPIO_INT_DISABLE) != 0,
		     "Interrupt should be disabled on port A");

	zassert_ok(gpio_emul_flags_get(b->port, b->pin, &flags_b));
	zassert_true((flags_b & GPIO_INT_DISABLE) != 0,
		     "Interrupt should be disabled on port B");
}
