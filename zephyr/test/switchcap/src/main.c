/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include <drivers/cros_switchcap.h>

#define SWITCHCAP_NODE DT_NODELABEL(switchcap)

static const struct device *const switchcap_dev = DEVICE_DT_GET(SWITCHCAP_NODE);
static const struct gpio_dt_spec enable_gpio =
	GPIO_DT_SPEC_GET(SWITCHCAP_NODE, enable_gpios);
static const struct gpio_dt_spec power_good_gpio =
	GPIO_DT_SPEC_GET(SWITCHCAP_NODE, power_good_gpios);

ZTEST(switchcap, test_init)
{
	gpio_flags_t flags;

	zassert_true(device_is_ready(switchcap_dev));

	zassert_ok(
		gpio_emul_flags_get(enable_gpio.port, enable_gpio.pin, &flags));
	zassert_true(flags & GPIO_OUTPUT);

	zassert_ok(gpio_emul_flags_get(power_good_gpio.port,
				       power_good_gpio.pin, &flags));
	zassert_true(flags & GPIO_INPUT);
}

ZTEST(switchcap, test_is_enabled_true)
{
	/* GPIO_INPUT is needed for the testing framework to set initial value
	 */
	zassert_ok(
		gpio_pin_configure_dt(&enable_gpio, GPIO_OUTPUT | GPIO_INPUT));
	zassert_ok(gpio_emul_input_set(enable_gpio.port, enable_gpio.pin, 1));
	zassert_true(cros_switchcap_is_enabled(switchcap_dev));
}

ZTEST(switchcap, test_is_enabled_false)
{
	/* GPIO_INPUT is needed for the testing framework to set initial value
	 */
	zassert_ok(
		gpio_pin_configure_dt(&enable_gpio, GPIO_OUTPUT | GPIO_INPUT));
	zassert_ok(gpio_emul_input_set(enable_gpio.port, enable_gpio.pin, 0));
	zassert_false(cros_switchcap_is_enabled(switchcap_dev));
}

ZTEST(switchcap, test_is_power_good_true)
{
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 1));
	zassert_true(cros_switchcap_is_power_good(switchcap_dev));
}

ZTEST(switchcap, test_is_power_good_false)
{
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 0));
	zassert_false(cros_switchcap_is_power_good(switchcap_dev));
}

ZTEST(switchcap, test_set_power_enable)
{
	zassert_ok(gpio_pin_set_dt(&enable_gpio, 0));
	zassert_ok(cros_switchcap_set_power(switchcap_dev, true));
	zassert_equal(gpio_emul_output_get(enable_gpio.port, enable_gpio.pin),
		      1);
}

ZTEST(switchcap, test_set_power_disable)
{
	zassert_ok(gpio_pin_set_dt(&enable_gpio, 1));
	zassert_ok(cros_switchcap_set_power(switchcap_dev, false));
	zassert_equal(gpio_emul_output_get(enable_gpio.port, enable_gpio.pin),
		      0);
}

ZTEST_SUITE(switchcap, NULL, NULL, NULL, NULL, NULL);
