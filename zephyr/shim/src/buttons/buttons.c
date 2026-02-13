/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "button.h"
#include "keyboard_protocol.h"
#include "mkbp_input_devices.h"
#include "power_button.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>

#include <dt-bindings/buttons.h>

LOG_MODULE_REGISTER(button, CONFIG_GPIO_LOG_LEVEL);

#define CROS_BUTTONS_DEV DEVICE_DT_GET(DT_NODELABEL(cros_buttons))

/* GPIO specs for direct button state reading */
#if DT_NODE_EXISTS(DT_NODELABEL(vol_up))
static const struct gpio_dt_spec vol_up_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(vol_up), gpios);
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(vol_down))
static const struct gpio_dt_spec vol_down_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(vol_down), gpios);
#endif

static uint32_t boot_button_state;

static void handle_volume_button(uint32_t button_code, int value)
{
	if (IS_ENABLED(CONFIG_MKBP_INPUT_DEVICES)) {
		mkbp_button_update(button_code, value);
	} else if (IS_ENABLED(HAS_TASK_KEYPROTO)) {
		keyboard_update_button(button_code, value);
	}
}

static void buttons_cb_handler(struct input_event *evt, void *user_data)
{
	if (evt->type != INPUT_EV_KEY) {
		return;
	}

	LOG_DBG("Button %s, code=%u, pin_state=%d", evt->dev->name, evt->code,
		evt->value);

	switch (evt->code) {
	case BUTTON_POWER:
		handle_power_button(evt->value);
		break;
	case BUTTON_VOLUME_UP:
		handle_volume_button(KEYBOARD_BUTTON_VOLUME_UP, evt->value);
		break;
	case BUTTON_VOLUME_DOWN:
		handle_volume_button(KEYBOARD_BUTTON_VOLUME_DOWN, evt->value);
		break;
	default:
		LOG_ERR("Unknown button code=%u", evt->code);
		break;
	}
}
INPUT_CALLBACK_DEFINE(CROS_BUTTONS_DEV, buttons_cb_handler, NULL);

static void read_boot_button_state(void)
{
	int pressed;

	boot_button_state = 0;

#if DT_NODE_EXISTS(DT_NODELABEL(vol_up))
	if (gpio_is_ready_dt(&vol_up_gpio)) {
		pressed = gpio_pin_get_dt(&vol_up_gpio);
		if (pressed > 0) {
			boot_button_state |= BIT(BUTTON_VOLUME_UP);
			LOG_INF("Volume UP pressed at boot");
		}
	}
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(vol_down))
	if (gpio_is_ready_dt(&vol_down_gpio)) {
		pressed = gpio_pin_get_dt(&vol_down_gpio);
		if (pressed > 0) {
			boot_button_state |= BIT(BUTTON_VOLUME_DOWN);
			LOG_INF("Volume DOWN pressed at boot");
		}
	}
#endif

	LOG_INF("Boot buttons: 0x%x", boot_button_state);
}

void button_init(void)
{
	/* Read initial button states before input subsystem starts */
	read_boot_button_state();

#ifdef CONFIG_PM_DEVICE_RUNTIME
	/*
	 * Keep buttons device permanently active to prevent PM from suspending
	 * it during async event delivery, which would drop critical button
	 * events (power, volume) during low-power states (S0ix/S3).
	 */
	pm_device_runtime_get(CROS_BUTTONS_DEV);
#endif
}

uint32_t button_get_boot_button(void)
{
	return boot_button_state;
}
