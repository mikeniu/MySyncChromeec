/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "stubs.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

void passthru_lid_open_to_pmic(void);
void passthru_ac_on_to_pmic(void);
void chipset_acok_passthru_interrupt(enum gpio_signal signal);
void reset_all_passthru_pmic_signal(void);

static void test_before(void *fixture)
{
	RESET_FAKE(extpower_is_present);
	RESET_FAKE(chipset_in_state);
}

ZTEST_SUITE(bluey_baseboard_chipset, NULL, NULL, test_before, NULL, NULL);

/* Test passing through lid open state to PMIC signal */
ZTEST(bluey_baseboard_chipset, test_passthru_lid_open_to_pmic)
{
	const struct gpio_dt_spec *lid_open =
		GPIO_DT_FROM_NODELABEL(gpio_lid_open);
	const struct gpio_dt_spec *pmic_lid_open =
		GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_lid_open_od);

	/* Set lid open high */
	gpio_pin_set_dt(lid_open, 1);
	passthru_lid_open_to_pmic();
	zassert_equal(gpio_pin_get_dt(pmic_lid_open), 1,
		      "PMIC lid open should follow lid open high");

	/* Set lid open low */
	gpio_pin_set_dt(lid_open, 0);
	passthru_lid_open_to_pmic();
	zassert_equal(gpio_pin_get_dt(pmic_lid_open), 0,
		      "PMIC lid open should follow lid open low");
}

/* Test passing through AC on state to PMIC signal */
ZTEST(bluey_baseboard_chipset, test_passthru_ac_on_to_pmic)
{
	const struct gpio_dt_spec *pmic_acok =
		GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_acok);

	extpower_is_present_fake.return_val = 1;
	passthru_ac_on_to_pmic();
	zassert_equal(gpio_pin_get_dt(pmic_acok), 1,
		      "PMIC ACOK should be high when extpower present");

	extpower_is_present_fake.return_val = 0;
	passthru_ac_on_to_pmic();
	zassert_equal(gpio_pin_get_dt(pmic_acok), 0,
		      "PMIC ACOK should be low when extpower not present");
}

/* Test ACOK passthrough interrupt handler to PMIC */
ZTEST(bluey_baseboard_chipset, test_chipset_acok_passthru_interrupt)
{
	const struct gpio_dt_spec *pmic_acok =
		GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_acok);

	/* In HARD_OFF state: should not update PMIC ACOK */
	gpio_pin_set_dt(pmic_acok, 0);
	chipset_in_state_fake.return_val = 1; /* CHIPSET_STATE_HARD_OFF */
	extpower_is_present_fake.return_val = 1;
	chipset_acok_passthru_interrupt(GPIO_AC_PRESENT);
	zassert_equal(gpio_pin_get_dt(pmic_acok), 0,
		      "ACOK should not be passed to PMIC during hard off");

	/* Not in HARD_OFF state: should update PMIC ACOK */
	chipset_in_state_fake.return_val = 0;
	extpower_is_present_fake.return_val = 1;
	chipset_acok_passthru_interrupt(GPIO_AC_PRESENT);
	zassert_equal(gpio_pin_get_dt(pmic_acok), 1,
		      "ACOK should be passed to PMIC when not in hard off");
}

/* Test enabling ACOK passthrough interrupt on HOOK_INIT */
ZTEST(bluey_baseboard_chipset, test_enable_acok_passthru_interrupt_init)
{
	const struct gpio_dt_spec *pmic_acok =
		GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_acok);

	/* In HARD_OFF state on init */
	gpio_pin_set_dt(pmic_acok, 0);
	chipset_in_state_fake.return_val = 1; /* CHIPSET_STATE_HARD_OFF */
	extpower_is_present_fake.return_val = 1;
	hook_notify(HOOK_INIT);
	zassert_equal(gpio_pin_get_dt(pmic_acok), 0,
		      "Init should not passthru ACOK during hard off");

	/* Not in HARD_OFF state on init */
	chipset_in_state_fake.return_val = 0;
	extpower_is_present_fake.return_val = 1;
	hook_notify(HOOK_INIT);
	zassert_equal(gpio_pin_get_dt(pmic_acok), 1,
		      "Init should passthru ACOK when not hard off");
}

/* Test resetting all PMIC passthrough signals */
ZTEST(bluey_baseboard_chipset, test_reset_all_passthru_pmic_signal)
{
	const struct gpio_dt_spec *pmic_acok =
		GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_acok);
	const struct gpio_dt_spec *pmic_lid_open =
		GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_lid_open_od);

	gpio_pin_set_dt(pmic_acok, 1);
	gpio_pin_set_dt(pmic_lid_open, 1);

	reset_all_passthru_pmic_signal();

	zassert_equal(gpio_pin_get_dt(pmic_acok), 0,
		      "PMIC ACOK should be reset to 0");
	zassert_equal(gpio_pin_get_dt(pmic_lid_open), 0,
		      "PMIC lid open should be reset to 0");
}
