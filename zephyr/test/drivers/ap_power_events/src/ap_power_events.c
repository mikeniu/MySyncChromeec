/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>

#define AP_PWR_EVENTS_NODE DT_NODELABEL(ap_power_events)

static const struct gpio_dt_spec test_gpios[] = {
	GPIO_DT_SPEC_GET_BY_IDX(AP_PWR_EVENTS_NODE, event_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(AP_PWR_EVENTS_NODE, event_gpios, 1),
};

static void ap_power_events_before(void *fixture)
{
	ARG_UNUSED(fixture);
	/* Reset GPIOs to de-asserted state before each test */
	for (size_t i = 0; i < ARRAY_SIZE(test_gpios); i++) {
		gpio_pin_set_dt(&test_gpios[i], 0);
	}
}

ZTEST_SUITE(ap_power_events, NULL, NULL, ap_power_events_before, NULL, NULL);

ZTEST(ap_power_events, test_startup_asserts_gpios)
{
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);

	for (size_t i = 0; i < ARRAY_SIZE(test_gpios); i++) {
		zassert_true(
			gpio_emul_output_get(test_gpios[i].port,
					     test_gpios[i].pin),
			"GPIO %zu should be asserted after AP_POWER_STARTUP",
			i);
	}
}

ZTEST(ap_power_events, test_pre_init_asserts_gpios)
{
	ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);

	for (size_t i = 0; i < ARRAY_SIZE(test_gpios); i++) {
		zassert_true(
			gpio_emul_output_get(test_gpios[i].port,
					     test_gpios[i].pin),
			"GPIO %zu should be asserted after AP_POWER_PRE_INIT",
			i);
	}
}

ZTEST(ap_power_events, test_hard_off_deasserts_gpios)
{
	/* First assert GPIOs */
	for (size_t i = 0; i < ARRAY_SIZE(test_gpios); i++) {
		gpio_pin_set_dt(&test_gpios[i], 1);
	}

	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);

	for (size_t i = 0; i < ARRAY_SIZE(test_gpios); i++) {
		zassert_false(
			gpio_emul_output_get(test_gpios[i].port,
					     test_gpios[i].pin),
			"GPIO %zu should be de-asserted after AP_POWER_HARD_OFF",
			i);
	}
}

ZTEST(ap_power_events, test_unrelated_event_no_change)
{
	/* Set GPIOs to a known state */
	for (size_t i = 0; i < ARRAY_SIZE(test_gpios); i++) {
		gpio_pin_set_dt(&test_gpios[i], 1);
	}

	/* Send an unrelated event */
	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);

	/* GPIOs should remain unchanged */
	for (size_t i = 0; i < ARRAY_SIZE(test_gpios); i++) {
		zassert_true(
			gpio_emul_output_get(test_gpios[i].port,
					     test_gpios[i].pin),
			"GPIO %zu should remain unchanged after unrelated event",
			i);
	}
}

ZTEST(ap_power_events, test_startup_after_hard_off)
{
	/* Assert GPIOs via startup */
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	for (size_t i = 0; i < ARRAY_SIZE(test_gpios); i++) {
		zassert_true(gpio_emul_output_get(test_gpios[i].port,
						  test_gpios[i].pin));
	}

	/* De-assert via hard off */
	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	for (size_t i = 0; i < ARRAY_SIZE(test_gpios); i++) {
		zassert_false(gpio_emul_output_get(test_gpios[i].port,
						   test_gpios[i].pin));
	}

	/* Re-assert via startup */
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	for (size_t i = 0; i < ARRAY_SIZE(test_gpios); i++) {
		zassert_true(gpio_emul_output_get(test_gpios[i].port,
						  test_gpios[i].pin));
	}
}
