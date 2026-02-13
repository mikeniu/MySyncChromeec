/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "button.h"
#include "keyboard_protocol.h"
#include "mkbp_input_devices.h"
#include "power_button.h"

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>

#include <dt-bindings/buttons.h>

LOG_MODULE_REGISTER(button, CONFIG_GPIO_LOG_LEVEL);

#define CROS_BUTTONS_DEV DEVICE_DT_GET(DT_NODELABEL(cros_buttons))

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
		if (IS_ENABLED(CONFIG_MKBP_INPUT_DEVICES)) {
			mkbp_button_update(KEYBOARD_BUTTON_VOLUME_UP,
					   evt->value);
		} else if (IS_ENABLED(HAS_TASK_KEYPROTO)) {
			keyboard_update_button(KEYBOARD_BUTTON_VOLUME_UP,
					       evt->value);
		}
		break;
	case BUTTON_VOLUME_DOWN:
		if (IS_ENABLED(CONFIG_MKBP_INPUT_DEVICES)) {
			mkbp_button_update(KEYBOARD_BUTTON_VOLUME_DOWN,
					   evt->value);
		} else if (IS_ENABLED(HAS_TASK_KEYPROTO)) {
			keyboard_update_button(KEYBOARD_BUTTON_VOLUME_DOWN,
					       evt->value);
		}
		break;
	default:
		LOG_ERR("Unknown button code=%u", evt->code);
		break;
	}
}
INPUT_CALLBACK_DEFINE(CROS_BUTTONS_DEV, buttons_cb_handler, NULL);

void button_init(void)
{
#ifdef CONFIG_PM_DEVICE
	/*
	 * Increment the runtime PM reference count for the buttons device,
	 * keeping it permanently active. The input subsystem delivers button
	 * events asynchronously via the registered callback, so the device must
	 * never be suspended by the PM runtime. Without this call the PM
	 * runtime could power down the device when it appears idle, causing
	 * button events (power, volume up/down) to be silently dropped until
	 * the device is resumed — potentially missing critical inputs such as
	 * the power button press during S0ix or S3 states.
	 */
	pm_device_runtime_get(CROS_BUTTONS_DEV);
#endif
}
