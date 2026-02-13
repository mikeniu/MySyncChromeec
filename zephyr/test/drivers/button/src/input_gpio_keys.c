/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "button.h"
#include "keyboard_protocol.h"
#include "mkbp_fifo.h"
#include "mkbp_input_devices.h"
#include "power_button.h"
#include "test/drivers/test_state.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <dt-bindings/buttons.h>

#define CROS_BUTTONS_DEV DEVICE_DT_GET(DT_NODELABEL(cros_buttons))

/* GPIO emulator nodes */
#define POWER_BUTTON_GPIO_NODE DT_NODELABEL(power_button)
#define VOL_UP_GPIO_NODE DT_NODELABEL(vol_up)
#define VOL_DOWN_GPIO_NODE DT_NODELABEL(vol_down)

/* Get GPIO specs from the button nodes */
static const struct gpio_dt_spec power_btn_gpio =
	GPIO_DT_SPEC_GET(POWER_BUTTON_GPIO_NODE, gpios);
static const struct gpio_dt_spec vol_up_gpio =
	GPIO_DT_SPEC_GET(VOL_UP_GPIO_NODE, gpios);
static const struct gpio_dt_spec vol_down_gpio =
	GPIO_DT_SPEC_GET(VOL_DOWN_GPIO_NODE, gpios);

/**
 * Helper function to simulate button press via GPIO emulation
 */
static void simulate_button_press(const struct gpio_dt_spec *gpio, bool pressed)
{
	int pin_value = pressed ? 0 : 1; /* Active LOW buttons */

	gpio_emul_input_set(gpio->port, gpio->pin, pin_value);
	/* Allow time for debounce and event processing */
	k_msleep(50);
}

/**
 * Helper to send input event directly to the button device
 */
static void send_button_event(uint16_t button_code, int value)
{
	input_report_key(CROS_BUTTONS_DEV, button_code, value, true, K_FOREVER);
	k_msleep(10);
}

static void *input_gpio_keys_setup(void)
{
	return NULL;
}

static void input_gpio_keys_before(void *f)
{
	ARG_UNUSED(f);

	/* Clear MKBP FIFO */
	mkbp_clear_fifo();

	/* Ensure all buttons are released */
	simulate_button_press(&power_btn_gpio, false);
	simulate_button_press(&vol_up_gpio, false);
	simulate_button_press(&vol_down_gpio, false);

	/* Re-initialize button subsystem */
	button_init();

	k_msleep(100);
}

ZTEST_SUITE(input_gpio_keys, drivers_predicate_post_main, input_gpio_keys_setup,
	    input_gpio_keys_before, NULL, NULL);

ZTEST(input_gpio_keys, test_cros_buttons_device_ready)
{
	zassert_true(device_is_ready(CROS_BUTTONS_DEV),
		     "cros_buttons device should be ready");
}

ZTEST(input_gpio_keys, test_power_button_press_release)
{
	/* Initially not pressed */
	zassert_equal(power_button_is_pressed(), 0,
		      "Power button should not be pressed initially");

	/* Simulate power button press */
	send_button_event(BUTTON_POWER, 1);

	zassert_equal(power_button_is_pressed(), 1,
		      "Power button should be pressed");

	/* Simulate power button release */
	send_button_event(BUTTON_POWER, 0);

	zassert_equal(power_button_is_pressed(), 0,
		      "Power button should be released");
}

ZTEST(input_gpio_keys, test_volume_up_button_event)
{
	uint32_t event_data = 0;

	/* Clear any pending events */
	while (mkbp_fifo_get_next_event((uint8_t *)&event_data,
					EC_MKBP_EVENT_BUTTON) > 0)
		;

	/* Press volume up */
	send_button_event(BUTTON_VOLUME_UP, 1);

	/* Check that MKBP event was generated */
	zassert_equal(4,
		      mkbp_fifo_get_next_event((uint8_t *)&event_data,
					       EC_MKBP_EVENT_BUTTON),
		      "Expected button event in MKBP FIFO");
	zassert_equal(event_data, BIT(EC_MKBP_VOL_UP),
		      "Expected volume up button press event");

	/* Release volume up */
	send_button_event(BUTTON_VOLUME_UP, 0);

	/* Check release event */
	zassert_equal(4,
		      mkbp_fifo_get_next_event((uint8_t *)&event_data,
					       EC_MKBP_EVENT_BUTTON),
		      "Expected button release event in MKBP FIFO");
	zassert_equal(event_data, 0, "Expected all buttons released");
}

ZTEST(input_gpio_keys, test_volume_down_button_event)
{
	uint32_t event_data = 0;

	/* Clear any pending events */
	while (mkbp_fifo_get_next_event((uint8_t *)&event_data,
					EC_MKBP_EVENT_BUTTON) > 0)
		;

	/* Press volume down */
	send_button_event(BUTTON_VOLUME_DOWN, 1);

	/* Check that MKBP event was generated */
	zassert_equal(4,
		      mkbp_fifo_get_next_event((uint8_t *)&event_data,
					       EC_MKBP_EVENT_BUTTON),
		      "Expected button event in MKBP FIFO");
	zassert_equal(event_data, BIT(EC_MKBP_VOL_DOWN),
		      "Expected volume down button press event");

	/* Release volume down */
	send_button_event(BUTTON_VOLUME_DOWN, 0);

	/* Check release event */
	zassert_equal(4,
		      mkbp_fifo_get_next_event((uint8_t *)&event_data,
					       EC_MKBP_EVENT_BUTTON),
		      "Expected button release event in MKBP FIFO");
	zassert_equal(event_data, 0, "Expected all buttons released");
}

ZTEST(input_gpio_keys, test_volume_buttons_simultaneous)
{
	uint32_t event_data = 0;

	/* Clear any pending events */
	while (mkbp_fifo_get_next_event((uint8_t *)&event_data,
					EC_MKBP_EVENT_BUTTON) > 0)
		;

	/* Press both volume buttons */
	send_button_event(BUTTON_VOLUME_UP, 1);
	send_button_event(BUTTON_VOLUME_DOWN, 1);

	/* Check that both buttons are registered */
	zassert_equal(4,
		      mkbp_fifo_get_next_event((uint8_t *)&event_data,
					       EC_MKBP_EVENT_BUTTON),
		      "Expected button event in MKBP FIFO");
	zassert_true((event_data & BIT(EC_MKBP_VOL_UP)) != 0,
		     "Volume up should be pressed");

	zassert_equal(4,
		      mkbp_fifo_get_next_event((uint8_t *)&event_data,
					       EC_MKBP_EVENT_BUTTON),
		      "Expected second button event in MKBP FIFO");
	zassert_true((event_data & BIT(EC_MKBP_VOL_DOWN)) != 0,
		     "Volume down should be pressed");

	/* Release both buttons */
	send_button_event(BUTTON_VOLUME_UP, 0);
	send_button_event(BUTTON_VOLUME_DOWN, 0);
}

ZTEST(input_gpio_keys, test_boot_button_state_none)
{
	uint32_t boot_buttons;

	/* Re-initialize with no buttons pressed */
	simulate_button_press(&power_btn_gpio, false);
	simulate_button_press(&vol_up_gpio, false);
	simulate_button_press(&vol_down_gpio, false);

	button_init();
	k_msleep(50);

	boot_buttons = button_get_boot_button();
	zassert_equal(boot_buttons, 0, "No buttons should be pressed at boot");
}

ZTEST(input_gpio_keys, test_boot_button_state_vol_up)
{
	uint32_t boot_buttons;

	/* Press volume up before init */
	simulate_button_press(&vol_up_gpio, true);
	simulate_button_press(&vol_down_gpio, false);
	simulate_button_press(&power_btn_gpio, false);

	button_init();
	k_msleep(50);

	boot_buttons = button_get_boot_button();
	zassert_true((boot_buttons & BIT(BUTTON_VOLUME_UP)) != 0,
		     "Volume up should be detected at boot");
	zassert_equal(boot_buttons & BIT(BUTTON_VOLUME_DOWN), 0,
		      "Volume down should not be detected at boot");

	/* Clean up */
	simulate_button_press(&vol_up_gpio, false);
}

ZTEST(input_gpio_keys, test_boot_button_state_vol_down)
{
	uint32_t boot_buttons;

	/* Press volume down before init */
	simulate_button_press(&vol_up_gpio, false);
	simulate_button_press(&vol_down_gpio, true);
	simulate_button_press(&power_btn_gpio, false);

	button_init();
	k_msleep(50);

	boot_buttons = button_get_boot_button();
	zassert_true((boot_buttons & BIT(BUTTON_VOLUME_DOWN)) != 0,
		     "Volume down should be detected at boot");
	zassert_equal(boot_buttons & BIT(BUTTON_VOLUME_UP), 0,
		      "Volume up should not be detected at boot");

	/* Clean up */
	simulate_button_press(&vol_down_gpio, false);
}

ZTEST(input_gpio_keys, test_boot_button_state_both)
{
	uint32_t boot_buttons;

	/* Press both volume buttons before init */
	simulate_button_press(&vol_up_gpio, true);
	simulate_button_press(&vol_down_gpio, true);
	simulate_button_press(&power_btn_gpio, false);

	button_init();
	k_msleep(50);

	boot_buttons = button_get_boot_button();
	zassert_true((boot_buttons & BIT(BUTTON_VOLUME_UP)) != 0,
		     "Volume up should be detected at boot");
	zassert_true((boot_buttons & BIT(BUTTON_VOLUME_DOWN)) != 0,
		     "Volume down should be detected at boot");

	/* Clean up */
	simulate_button_press(&vol_up_gpio, false);
	simulate_button_press(&vol_down_gpio, false);
}

#ifdef CONFIG_PM_DEVICE_RUNTIME
ZTEST(input_gpio_keys, test_pm_device_stays_active)
{
	enum pm_device_state state;

	/* Check that cros_buttons device is kept active */
	pm_device_state_get(CROS_BUTTONS_DEV, &state);
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE,
		      "Buttons device should be kept active for runtime PM");

	/* Verify device stays active even after events */
	send_button_event(BUTTON_POWER, 1);
	send_button_event(BUTTON_POWER, 0);

	pm_device_state_get(CROS_BUTTONS_DEV, &state);
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE,
		      "Buttons device should remain active after events");
}
#endif

ZTEST(input_gpio_keys, test_unknown_button_code)
{
	/* Send an unknown button code - should be logged but not crash */
	input_report_key(CROS_BUTTONS_DEV, 99, 1, true, K_FOREVER);
	k_msleep(10);

	/* System should still be functional */
	send_button_event(BUTTON_POWER, 1);
	zassert_equal(power_button_is_pressed(), 1,
		      "Power button should still work after unknown code");
	send_button_event(BUTTON_POWER, 0);
}

ZTEST(input_gpio_keys, test_power_button_multiple_press_release)
{
	/* Test rapid press/release sequence */
	for (int i = 0; i < 5; i++) {
		send_button_event(BUTTON_POWER, 1);
		zassert_equal(power_button_is_pressed(), 1,
			      "Power button press %d failed", i);

		send_button_event(BUTTON_POWER, 0);
		zassert_equal(power_button_is_pressed(), 0,
			      "Power button release %d failed", i);
	}
}

ZTEST(input_gpio_keys, test_volume_button_state_persistence)
{
	uint32_t event_data = 0;

	/* Clear events */
	while (mkbp_fifo_get_next_event((uint8_t *)&event_data,
					EC_MKBP_EVENT_BUTTON) > 0)
		;

	/* Press volume up and hold */
	send_button_event(BUTTON_VOLUME_UP, 1);

	/* Verify press event */
	zassert_equal(4, mkbp_fifo_get_next_event((uint8_t *)&event_data,
						  EC_MKBP_EVENT_BUTTON));
	zassert_equal(event_data, BIT(EC_MKBP_VOL_UP));

	/* Press volume down while up is still held */
	send_button_event(BUTTON_VOLUME_DOWN, 1);

	/* Verify both buttons are reported */
	zassert_equal(4, mkbp_fifo_get_next_event((uint8_t *)&event_data,
						  EC_MKBP_EVENT_BUTTON));
	zassert_equal(event_data, BIT(EC_MKBP_VOL_UP) | BIT(EC_MKBP_VOL_DOWN));

	/* Release volume up first */
	send_button_event(BUTTON_VOLUME_UP, 0);

	/* Verify only volume down is still pressed */
	zassert_equal(4, mkbp_fifo_get_next_event((uint8_t *)&event_data,
						  EC_MKBP_EVENT_BUTTON));
	zassert_equal(event_data, BIT(EC_MKBP_VOL_DOWN));

	/* Release volume down */
	send_button_event(BUTTON_VOLUME_DOWN, 0);

	/* Verify all released */
	zassert_equal(4, mkbp_fifo_get_next_event((uint8_t *)&event_data,
						  EC_MKBP_EVENT_BUTTON));
	zassert_equal(event_data, 0);
}

ZTEST(input_gpio_keys, test_button_init_called)
{
	/* Verify button_init() can be called multiple times without issue */
	button_init();
	k_msleep(10);
	button_init();
	k_msleep(10);

	/* Verify buttons still work */
	send_button_event(BUTTON_POWER, 1);
	zassert_equal(power_button_is_pressed(), 1,
		      "Power button should work after re-init");
	send_button_event(BUTTON_POWER, 0);
}
