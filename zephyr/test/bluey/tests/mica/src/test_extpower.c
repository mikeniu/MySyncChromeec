/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "extpower.h"
#include "gpio.h"
#include "stubs.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static uint32_t fake_board_id;

static int fake_cbi_get_board_version(uint32_t *version)
{
	*version = fake_board_id;
	return EC_SUCCESS;
}

static void test_extpower_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(cbi_get_board_version);
	fake_board_id = 0;
	cbi_get_board_version_fake.custom_fake = fake_cbi_get_board_version;

	const struct gpio_dt_spec *a =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_a_chg_led_pg_odl);
	const struct gpio_dt_spec *b =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_b_chg_led_pg_odl);
	const struct gpio_dt_spec *pgood =
		GPIO_DT_FROM_NODELABEL(gpio_pgood_acok_odl);

	gpio_emul_input_set(a->port, a->pin, 1);
	gpio_emul_input_set(b->port, b->pin, 1);
	gpio_emul_input_set(pgood->port, pgood->pin, 1);
}

ZTEST_SUITE(mica_extpower, NULL, NULL, test_extpower_before, NULL, NULL);

/* Test external power detection for early board versions */
ZTEST(mica_extpower, test_is_present_early_board)
{
	const struct gpio_dt_spec *a =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_a_chg_led_pg_odl);
	const struct gpio_dt_spec *b =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_b_chg_led_pg_odl);

	fake_board_id = 1;
	zassert_equal(board_extpower_is_present(), 0);

	/* Pulling pin low (0) activates active-low signal -> logical 1 */
	gpio_emul_input_set(a->port, a->pin, 0);
	zassert_equal(board_extpower_is_present(), 1);

	gpio_emul_input_set(a->port, a->pin, 1);
	gpio_emul_input_set(b->port, b->pin, 0);
	zassert_equal(board_extpower_is_present(), 1);

	/* Test CBI error fallback */
	cbi_get_board_version_fake.custom_fake = NULL;
	cbi_get_board_version_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(board_extpower_is_present(), 1);
}

/* Test external power detection for late board versions */
ZTEST(mica_extpower, test_is_present_late_board)
{
	const struct gpio_dt_spec *pgood =
		GPIO_DT_FROM_NODELABEL(gpio_pgood_acok_odl);

	fake_board_id = 2;
	zassert_equal(board_extpower_is_present(), 0);

	gpio_emul_input_set(pgood->port, pgood->pin, 0);
	zassert_equal(board_extpower_is_present(), 1);

	gpio_emul_input_set(pgood->port, pgood->pin, 1);
	zassert_equal(board_extpower_is_present(), 0);
}

/* Test external power interrupts for early board versions */
ZTEST(mica_extpower, test_interrupts_early_board)
{
	const struct gpio_dt_spec *a =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_a_chg_led_pg_odl);
	const struct gpio_dt_spec *b =
		GPIO_DT_FROM_NODELABEL(gpio_smb2360_b_chg_led_pg_odl);
	gpio_flags_t flags;

	fake_board_id = 0;
	board_extpower_enable_interrupt();
	zassert_ok(gpio_emul_flags_get(a->port, a->pin, &flags));
	zassert_true((flags & GPIO_INT_ENABLE) != 0,
		     "Interrupt should be enabled on SMB2360 A");
	zassert_ok(gpio_emul_flags_get(b->port, b->pin, &flags));
	zassert_true((flags & GPIO_INT_ENABLE) != 0,
		     "Interrupt should be enabled on SMB2360 B");

	board_extpower_disable_interrupt();
	zassert_ok(gpio_emul_flags_get(a->port, a->pin, &flags));
	zassert_true((flags & GPIO_INT_DISABLE) != 0,
		     "Interrupt should be disabled on SMB2360 A");
	zassert_ok(gpio_emul_flags_get(b->port, b->pin, &flags));
	zassert_true((flags & GPIO_INT_DISABLE) != 0,
		     "Interrupt should be disabled on SMB2360 B");
}

/* Test external power interrupts for late board versions */
ZTEST(mica_extpower, test_interrupts_late_board)
{
	const struct gpio_dt_spec *pgood =
		GPIO_DT_FROM_NODELABEL(gpio_pgood_acok_odl);
	gpio_flags_t flags;

	fake_board_id = 2;
	board_extpower_enable_interrupt();
	zassert_ok(gpio_emul_flags_get(pgood->port, pgood->pin, &flags));
	zassert_true((flags & GPIO_INT_ENABLE) != 0,
		     "Interrupt should be enabled on PGOOD ACOK");

	board_extpower_disable_interrupt();
	zassert_ok(gpio_emul_flags_get(pgood->port, pgood->pin, &flags));
	zassert_true((flags & GPIO_INT_DISABLE) != 0,
		     "Interrupt should be disabled on PGOOD ACOK");
}
