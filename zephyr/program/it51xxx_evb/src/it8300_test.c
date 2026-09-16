/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>

#include <soc_common.h>

#include "common.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

void it8300_gpio_interrupt(enum gpio_signal signal)
{
	printk("IT8300 GPIO interrupt: signal=%d level=%d\n", signal,
	       gpio_get_level(signal));
}

struct it8300_gpio_test_pin {
	const char *name;
	enum gpio_signal signal;
	gpio_flags_t init_flags;
};

#define IT8300_TEST_PIN(name_, label)                                          \
	{                                                                      \
		.name = name_,                                                 \
		.signal = GPIO_SIGNAL(DT_NODELABEL(label)),                    \
		.init_flags = DT_GPIO_FLAGS(DT_NODELABEL(label), gpios),       \
	}

static const struct it8300_gpio_test_pin it8300_gpio_test_pins[] = {
	IT8300_TEST_PIN("GPA0", it8300_irq_out),
	IT8300_TEST_PIN("GPB0", it8300_gpb_out),
	IT8300_TEST_PIN("GPC0", it8300_gpc_out),
	IT8300_TEST_PIN("GPD0", it8300_gpd_out),
	IT8300_TEST_PIN("GPE0", it8300_gpe_out),
};

static int it8300_test_configure(const struct it8300_gpio_test_pin *test_pin)
{
	static const struct {
		const char *name;
		gpio_flags_t flags;
	} cases[] = {
		{ "output low", GPIO_OUTPUT_LOW },
		{ "output high", GPIO_OUTPUT_HIGH },
		{ "input pull-up", GPIO_INPUT | GPIO_PULL_UP },
		{ "input pull-down", GPIO_INPUT | GPIO_PULL_DOWN },
		{ "input no pull", GPIO_INPUT },
	};
	const gpio_flags_t mask = GPIO_INPUT | GPIO_OUTPUT | GPIO_PULL_UP |
		GPIO_PULL_DOWN | GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH;
	const struct gpio_dt_spec *gpio = gpio_get_dt_spec(test_pin->signal);
	bool passed = true;
	int rv;

	if (!gpio_is_ready_dt(gpio)) {
		printk("IT8300 %s configure test: device not ready\n",
		       test_pin->name);
		return EC_ERROR_UNKNOWN;
	}

	for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
		gpio_flags_t actual = 0;

		rv = gpio_pin_configure_dt(gpio, cases[i].flags);
		if (rv != 0) {
			printk("IT8300 %s configure %s failed: %d\n",
			       test_pin->name, cases[i].name, rv);
			passed = false;
			break;
		}

		rv = gpio_pin_get_config_dt(gpio, &actual);
		if (rv != 0) {
			printk("IT8300 %s get config %s failed: %d\n",
			       test_pin->name, cases[i].name, rv);
			passed = false;
			break;
		}

		if ((actual & mask) != (cases[i].flags & mask)) {
			printk("IT8300 %s config %s mismatch: expected=0x%x actual=0x%x\n",
			       test_pin->name, cases[i].name,
			       (unsigned int)(cases[i].flags & mask),
			       (unsigned int)(actual & mask));
			passed = false;
			break;
		}
		printk("IT8300 %s configure/get-config %s passed\n",
		       test_pin->name, cases[i].name);
	}

	/* Restore the configuration */
	rv = gpio_pin_configure_dt(gpio, test_pin->init_flags);
	if (rv != 0) {
		printk("IT8300 %s restore DTS config failed: %d\n",
		       test_pin->name, rv);
		return rv;
	}

	return passed ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

#define IT8300_LOOPBACK_PIN_COUNT 7U

static const struct device *const it8300_gpb =
	DEVICE_DT_GET(DT_NODELABEL(ioex_it8300_gpb));
static const struct device *const it8300_gpc =
	DEVICE_DT_GET(DT_NODELABEL(ioex_it8300_gpc));

static int it8300_loopback_check(gpio_pin_t pin, int expected_level)
{
	int level, rv;

	rv = gpio_pin_set(it8300_gpb, pin, expected_level);
	if (rv != 0) {
		printk("IT8300 GPB%d set %s failed: %d\n", pin,
		       expected_level ? "high" : "low", rv);
		return rv;
	}

	k_sleep(K_MSEC(1));

	level = gpio_pin_get(it8300_gpc, pin);
	if (level < 0) {
		printk("IT8300 GPC%d read failed: %d\n", pin, level);
		return level;
	}
	if (level != expected_level) {
		printk("IT8300 loopback GPB%d -> GPC%d failed: expected %s, got %s\n",
		       pin, pin, expected_level ? "high" : "low",
		       level ? "high" : "low");
		return -EIO;
	}

	return 0;
}

static void it8300_test(void)
{
	printk("[8300]IT8300 interrupt test\n");

	if (gpio_enable_dt_interrupt(
		    GPIO_INT_FROM_NODELABEL(int_it8300_gpio)) != 0) {
		printk("Failed to enable IT8300 GPIO interrupt\n");
	}

	printk("[8300]IT8300 pin configure test\n");

	for (size_t i = 0; i < ARRAY_SIZE(it8300_gpio_test_pins); ++i) {
		if (it8300_test_configure(&it8300_gpio_test_pins[i]) != 0) {
			continue;
		}
	}

	printk("[8300]IT8300 loopback test: connect GPB0..6 to GPC0..6 one-to-one\n");
	int failed = 0;
	int rv;

	if (!device_is_ready(it8300_gpb) || !device_is_ready(it8300_gpc)) {
		printk("IT8300 loopback test failed: GPIO device is not ready\n");
		return;
	}

	for (gpio_pin_t pin = 0U; pin < IT8300_LOOPBACK_PIN_COUNT; pin++) {
		rv = gpio_pin_configure(it8300_gpb, pin, GPIO_OUTPUT_LOW);
		if (rv != 0) {
			printk("IT8300 GPB%d output configuration failed: %d\n", pin, rv);
			return;
		}

		rv = gpio_pin_configure(it8300_gpc, pin, GPIO_INPUT);
		if (rv != 0) {
			printk("IT8300 GPC%d input configuration failed: %d\n", pin, rv);
			return;
		}
	}

	for (gpio_pin_t pin = 0U; pin < IT8300_LOOPBACK_PIN_COUNT; pin++) {
		if (it8300_loopback_check(pin, 0) != 0 ||
		    it8300_loopback_check(pin, 1) != 0 ||
		    it8300_loopback_check(pin, 0) != 0) {
			failed++;
			continue;
		}

		printk("IT8300 loopback GPB%d -> GPC%d passed\n", pin, pin);
	}

	printk("IT8300 loopback test complete: %d passed, %d failed\n",
	       IT8300_LOOPBACK_PIN_COUNT - failed, failed);
}
DECLARE_HOOK(HOOK_INIT, it8300_test, HOOK_PRIO_DEFAULT);
