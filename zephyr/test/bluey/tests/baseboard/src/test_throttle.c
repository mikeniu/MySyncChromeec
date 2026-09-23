/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "gpio.h"
#include "hooks.h"
#include "stubs.h"
#include "throttle_ap.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

void chipset_throttle_cpu(int throttle);

static void test_before(void *fixture)
{
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(throttle_ap_config_prochot);
}

ZTEST_SUITE(bluey_baseboard_throttle, NULL, NULL, test_before, NULL, NULL);

/* Test throttle CPU does not trigger when chipset is not ON */
ZTEST(bluey_baseboard_throttle, test_throttle_cpu_not_on)
{
	chipset_in_state_fake.return_val = 0; /* Not CHIPSET_STATE_ON */

	/* Should do nothing and return when chipset is not in S0 */
	chipset_throttle_cpu(1);
	zassert_equal(chipset_in_state_fake.call_count, 1);
	zassert_equal(chipset_in_state_fake.arg0_val, CHIPSET_STATE_ON);

	chipset_throttle_cpu(0);
	zassert_equal(chipset_in_state_fake.call_count, 2);
	zassert_equal(chipset_in_state_fake.arg0_val, CHIPSET_STATE_ON);
}

/* Test throttle and unthrottle CPU when chipset is ON */
ZTEST(bluey_baseboard_throttle, test_throttle_cpu_on_throttle_and_unthrottle)
{
	chipset_in_state_fake.return_val = 1; /* CHIPSET_STATE_ON */

	const struct gpio_dt_spec *spec = gpio_get_dt_spec(GPIO_CPU_PROCHOT);
	gpio_flags_t flags;

	/* Enable throttle */
	chipset_throttle_cpu(1);
	zassert_equal(gpio_get_level(GPIO_CPU_PROCHOT), 0,
		      "PROCHOT should be driven active-low (0) when throttled");
	zassert_ok(gpio_emul_flags_get(spec->port, spec->pin, &flags));
	zassert_true((flags & GPIO_OUTPUT) != 0,
		     "PROCHOT flags should have GPIO_OUTPUT when throttled");

	/* Disable throttle */
	chipset_throttle_cpu(0);
	zassert_ok(gpio_emul_flags_get(spec->port, spec->pin, &flags));
	zassert_true((flags & GPIO_INPUT) != 0,
		     "PROCHOT flags should have GPIO_INPUT when unthrottled");
}

/* Test throttle initialization on HOOK_INIT */
ZTEST(bluey_baseboard_throttle, test_init_throttle)
{
	hook_notify(HOOK_INIT);
	zassert_true(
		throttle_ap_config_prochot_fake.call_count > 0,
		"throttle_ap_config_prochot should be called on HOOK_INIT");
}
