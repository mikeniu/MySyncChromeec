/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <drivers/btn_ign.h>

static const struct gpio_dt_spec btn_ign_gpio =
	GPIO_DT_SPEC_GET(DT_INST(0, cros_ec_btn_ign), btn_ign_gpios);

static void test_btn_ign_init(void *data)
{
	gpio_emul_input_set_dt(&btn_ign_gpio, 0);
}

static int btn_ign_get()
{
	return gpio_emul_output_get_dt(&btn_ign_gpio);
}

ZTEST(btn_ign_out, test_activate)
{
	btn_ign_activate();
	zassert_equal(btn_ign_get(), 1,
		      "GPIO pin should be high after activation");
}

ZTEST(btn_ign_out, test_deactivate)
{
	btn_ign_activate();
	k_sleep(K_MSEC(100));
	btn_ign_deactivate();

	k_sleep(K_MSEC(1000));
	zassert_equal(btn_ign_get(), 1, "GPIO should still be high");

	k_sleep(K_MSEC(1500));
	zassert_equal(btn_ign_get(), 0,
		      "GPIO pin should be low after deactivation");
}

ZTEST(btn_ign_out, test_activate_deactivate_consecutive_calls)
{
	btn_ign_activate();
	zassert_equal(btn_ign_get(), 1,
		      "GPIO should be high after first activate");

	btn_ign_deactivate();
	btn_ign_activate();

	k_sleep(K_MSEC(2500));
	zassert_equal(
		btn_ign_get(), 1,
		"GPIO should still be high due to cancelled deactivation");

	btn_ign_deactivate();
	k_sleep(K_MSEC(2500));
	zassert_equal(btn_ign_get(), 0,
		      "GPIO should be low after final deactivation");
}

ZTEST(btn_ign_out, test_deactivate_multiple_calls)
{
	btn_ign_activate();
	zassert_equal(btn_ign_get(), 1, "GPIO should be high after activate");

	btn_ign_deactivate();
	k_sleep(K_MSEC(1000));
	btn_ign_deactivate();

	k_sleep(K_MSEC(1500));
	zassert_equal(btn_ign_get(), 0,
		      "GPIO should be low 2s after first deactivation");
}

ZTEST_SUITE(btn_ign_out, NULL, NULL, test_btn_ign_init, NULL, NULL);
