/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include <drivers/cros_switchcap.h>

#define SWITCHCAP_NODE DT_NODELABEL(switchcap)

static const struct device *const switchcap_dev = DEVICE_DT_GET(SWITCHCAP_NODE);
static const struct gpio_dt_spec enable_gpio =
	GPIO_DT_SPEC_GET(SWITCHCAP_NODE, enable_gpios);
static const struct adc_dt_spec power_good_adc =
	ADC_DT_SPEC_GET(SWITCHCAP_NODE);

static int adc_error_func(const struct device *dev, unsigned int chan,
			  void *data, uint32_t *result)
{
	return -EIO;
}

ZTEST(switchcap_adc, test_init)
{
	gpio_flags_t flags;

	zassert_true(device_is_ready(switchcap_dev));

	zassert_ok(
		gpio_emul_flags_get(enable_gpio.port, enable_gpio.pin, &flags));
	zassert_true(flags & GPIO_OUTPUT);

	zassert_true(adc_is_ready_dt(&power_good_adc));
}

ZTEST(switchcap_adc, test_is_power_good_true)
{
	zassert_ok(adc_emul_const_value_set(power_good_adc.dev,
					    power_good_adc.channel_id, 2500));
	zassert_true(cros_switchcap_is_power_good(switchcap_dev));
}

ZTEST(switchcap_adc, test_is_power_good_false)
{
	zassert_ok(adc_emul_const_value_set(power_good_adc.dev,
					    power_good_adc.channel_id, 1500));
	zassert_false(cros_switchcap_is_power_good(switchcap_dev));

	zassert_ok(adc_emul_const_value_set(power_good_adc.dev,
					    power_good_adc.channel_id, 2000));
	zassert_false(cros_switchcap_is_power_good(switchcap_dev));
}

ZTEST(switchcap_adc, test_is_power_good_adc_error)
{
	zassert_ok(adc_emul_value_func_set(power_good_adc.dev,
					   power_good_adc.channel_id,
					   adc_error_func, NULL));
	zassert_false(cros_switchcap_is_power_good(switchcap_dev));
}

ZTEST_SUITE(switchcap_adc, NULL, NULL, NULL, NULL, NULL);
