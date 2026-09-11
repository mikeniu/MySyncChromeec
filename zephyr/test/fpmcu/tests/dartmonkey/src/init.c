/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/fff.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>

FAKE_VALUE_FUNC(int, pinctrl_configure_pins, const pinctrl_soc_pin_t *, uint8_t,
		uintptr_t);

static struct gpio_callback highside_cb;

extern bool checked_is_prod;

int init_spi_select(void);

static void highside_handler(const struct device *port,
			     struct gpio_callback *cb, gpio_port_pins_t pins)
{
	const struct device *spi_sel_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(fp_spi_sel), gpios));
	const gpio_port_pins_t spi_sel_pin =
		DT_GPIO_PIN(DT_NODELABEL(fp_spi_sel), gpios);

	if (IS_ENABLED(CONFIG_TEST_BOARD_INIT_DEV)) {
		gpio_emul_input_set(spi_sel_gpio, spi_sel_pin, 0);
	}
}

static int test_early_init(void)
{
	const struct device *spi_sel_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(fp_spi_sel), gpios));
	const gpio_port_pins_t spi_sel_pin =
		DT_GPIO_PIN(DT_NODELABEL(fp_spi_sel), gpios);
	const struct device *highside_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(div_highside), gpios));
	const gpio_port_pins_t highside_pin =
		DT_GPIO_PIN(DT_NODELABEL(div_highside), gpios);

	RESET_FAKE(pinctrl_configure_pins);
	checked_is_prod = false;

	if (IS_ENABLED(CONFIG_TEST_BOARD_INIT_PROD)) {
		gpio_emul_input_set(spi_sel_gpio, spi_sel_pin, 1);
	} else if (IS_ENABLED(CONFIG_TEST_BOARD_INIT_DEV)) {
		gpio_init_callback(&highside_cb, highside_handler,
				   BIT(highside_pin));
		gpio_add_callback(highside_gpio, &highside_cb);
		gpio_emul_input_set(spi_sel_gpio, spi_sel_pin, 0);
	}

	return 0;
}
SYS_INIT(test_early_init, POST_KERNEL, 79);

ZTEST_SUITE(dartmonkey_init, NULL, NULL, NULL, NULL, NULL);

ZTEST(dartmonkey_init, test_init_spi_select)
{
	const struct device *highside_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(div_highside), gpios));
	const gpio_port_pins_t highside_pin =
		DT_GPIO_PIN(DT_NODELABEL(div_highside), gpios);
	const struct device *spi_sel_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(fp_spi_sel), gpios));
	const gpio_port_pins_t spi_sel_pin =
		DT_GPIO_PIN(DT_NODELABEL(fp_spi_sel), gpios);
	gpio_flags_t flags;

	if (IS_ENABLED(CONFIG_TEST_BOARD_INIT_PROD)) {
		zassert_equal(
			pinctrl_configure_pins_fake.call_count, 1,
			"pinctrl_configure_pins should be called for prod");
	} else if (IS_ENABLED(CONFIG_TEST_BOARD_INIT_DEV)) {
		zassert_equal(
			pinctrl_configure_pins_fake.call_count, 0,
			"pinctrl_configure_pins should NOT be called for dev");
	}

	/* Verify highside was cleared (set to 0) */
	zassert_equal(gpio_emul_output_get(highside_gpio, highside_pin), 0,
		      "Highside should be cleared after init_spi_select");

	/* Verify spi_sel pull-up was cleared and pin configured as input */
	zassert_equal(gpio_emul_flags_get(spi_sel_gpio, spi_sel_pin, &flags),
		      0);
	zassert_equal(flags & GPIO_PULL_UP, 0,
		      "Pull-up should be disabled after init_spi_select");
	zassert_equal(flags & GPIO_INPUT, GPIO_INPUT,
		      "Pin should remain configured as input");
}

ZTEST(dartmonkey_init, test_init_spi_select_spi_sel_not_ready)
{
	const struct device *spi_sel_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(fp_spi_sel), gpios));
	const struct device *highside_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(div_highside), gpios));
	const gpio_port_pins_t highside_pin =
		DT_GPIO_PIN(DT_NODELABEL(div_highside), gpios);
	struct device_state *state = (struct device_state *)spi_sel_gpio->state;
	uint8_t original_res = state->init_res;

	RESET_FAKE(pinctrl_configure_pins);
	checked_is_prod = false;

	/* Simulate spi_sel device not ready */
	state->init_res = 1;
	zassert_false(device_is_ready(spi_sel_gpio),
		      "spi_sel device should not be ready");

	zassert_equal(
		init_spi_select(), -ENODEV,
		"init_spi_select should return -ENODEV when spi_sel is not ready");
	zassert_equal(pinctrl_configure_pins_fake.call_count, 0,
		      "pinctrl_configure_pins should not be called");
	zassert_equal(gpio_emul_output_get(highside_gpio, highside_pin), 0,
		      "highside should remain 0");

	/* Restore device state */
	state->init_res = original_res;
	zassert_true(device_is_ready(spi_sel_gpio),
		     "spi_sel device should be ready again");
}

ZTEST(dartmonkey_init, test_init_spi_select_highside_not_ready)
{
	const struct device *highside_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(div_highside), gpios));
	const gpio_port_pins_t highside_pin =
		DT_GPIO_PIN(DT_NODELABEL(div_highside), gpios);
	struct device_state *state =
		(struct device_state *)highside_gpio->state;
	uint8_t original_res = state->init_res;

	RESET_FAKE(pinctrl_configure_pins);
	checked_is_prod = false;

	/* Simulate highside device not ready */
	state->init_res = 1;
	zassert_false(device_is_ready(highside_gpio),
		      "highside device should not be ready");

	zassert_equal(
		init_spi_select(), -ENODEV,
		"init_spi_select should return -ENODEV when highside is not ready");
	zassert_equal(pinctrl_configure_pins_fake.call_count, 0,
		      "pinctrl_configure_pins should not be called");
	zassert_equal(gpio_emul_output_get(highside_gpio, highside_pin), 0,
		      "highside should remain 0");

	/* Restore device state */
	state->init_res = original_res;
	zassert_true(device_is_ready(highside_gpio),
		     "highside device should be ready again");
}
