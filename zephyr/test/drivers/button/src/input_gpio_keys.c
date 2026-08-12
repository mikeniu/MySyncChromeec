/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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

ZTEST_SUITE(input_gpio_keys, drivers_predicate_post_main, NULL,
	    input_gpio_keys_before, NULL, NULL);

ZTEST(input_gpio_keys, test_cros_buttons_device_ready)
{
	zassert_true(device_is_ready(CROS_BUTTONS_DEV),
		     "cros_buttons device should be ready");
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

/*
 * The following tests drive the emulated GPIO pins declared in devicetree and
 * verify - through the driver's real reporting path (power button state and the
 * MKBP FIFO) - that toggling a pin generates the input event carrying the
 * zephyr,code declared for that pin's devicetree node. Unlike the tests that
 * inject codes directly via input_report_key(), these exercise the full chain:
 * GPIO edge -> input-gpio-keys driver -> reported zephyr,code -> button
 * handler. If the devicetree zephyr,code does not match the code the handler
 * acts on, these tests fail, surfacing the misconfiguration.
 */

ZTEST(input_gpio_keys, test_gpio_toggle_power_button_reported)
{
	/* Precondition: power button not pressed */
	zassert_equal(power_button_is_pressed(), 0,
		      "power button should start released");

	/* Toggle the power_button GPIO pin (active low: pressed => raw 0) */
	simulate_button_press(&power_btn_gpio, true);

	zassert_equal(
		power_button_is_pressed(), 1,
		"toggling power_button GPIO should be reported as a press "
		"(zephyr,code must map to BUTTON_POWER)");

	/* Release the GPIO pin */
	simulate_button_press(&power_btn_gpio, false);

	zassert_equal(power_button_is_pressed(), 0,
		      "releasing power_button GPIO should be reported as a "
		      "release");
}

ZTEST(input_gpio_keys, test_gpio_toggle_vol_up_reported)
{
	uint32_t event_data = 0;

	/* Drain any pending button events */
	while (mkbp_fifo_get_next_event((uint8_t *)&event_data,
					EC_MKBP_EVENT_BUTTON) > 0)
		;

	/* Toggle the vol_up GPIO pin (active low: pressed => raw 0) */
	simulate_button_press(&vol_up_gpio, true);

	zassert_equal(4,
		      mkbp_fifo_get_next_event((uint8_t *)&event_data,
					       EC_MKBP_EVENT_BUTTON),
		      "toggling vol_up GPIO should generate an MKBP button "
		      "event (zephyr,code must map to BUTTON_VOLUME_UP)");
	zassert_equal(event_data, BIT(EC_MKBP_VOL_UP),
		      "vol_up GPIO press should report volume-up");

	/* Release the GPIO pin */
	simulate_button_press(&vol_up_gpio, false);

	zassert_equal(4,
		      mkbp_fifo_get_next_event((uint8_t *)&event_data,
					       EC_MKBP_EVENT_BUTTON),
		      "releasing vol_up GPIO should generate an MKBP event");
	zassert_equal(event_data, 0,
		      "vol_up GPIO release should report all buttons released");
}

ZTEST(input_gpio_keys, test_gpio_toggle_vol_down_reported)
{
	uint32_t event_data = 0;

	/* Drain any pending button events */
	while (mkbp_fifo_get_next_event((uint8_t *)&event_data,
					EC_MKBP_EVENT_BUTTON) > 0)
		;

	/* Toggle the vol_down GPIO pin (active low: pressed => raw 0) */
	simulate_button_press(&vol_down_gpio, true);

	zassert_equal(4,
		      mkbp_fifo_get_next_event((uint8_t *)&event_data,
					       EC_MKBP_EVENT_BUTTON),
		      "toggling vol_down GPIO should generate an MKBP button "
		      "event (zephyr,code must map to BUTTON_VOLUME_DOWN)");
	zassert_equal(event_data, BIT(EC_MKBP_VOL_DOWN),
		      "vol_down GPIO press should report volume-down");

	/* Release the GPIO pin */
	simulate_button_press(&vol_down_gpio, false);

	zassert_equal(4,
		      mkbp_fifo_get_next_event((uint8_t *)&event_data,
					       EC_MKBP_EVENT_BUTTON),
		      "releasing vol_down GPIO should generate an MKBP event");
	zassert_equal(event_data, 0,
		      "vol_down GPIO release should report all buttons "
		      "released");
}
