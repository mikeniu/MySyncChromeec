/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "keyboard_protocol.h"
#include "mkbp_input_devices.h"
#include "power_button.h"

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>

#include <dt-bindings/buttons.h>

LOG_MODULE_REGISTER(button, CONFIG_GPIO_LOG_LEVEL);

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
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(cros_buttons)), buttons_cb_handler, NULL);

int buttons_init(void)
{
	pm_device_runtime_get(DEVICE_DT_GET(DT_N_S_buttons));

	return 0;
}
SYS_INIT(buttons_init, APPLICATION, CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY);
