/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "button.h"
#include "keyboard_protocol.h"
#include "mkbp_input_devices.h"
#include "power_button.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/shell/shell.h>

#include <dt-bindings/buttons.h>

LOG_MODULE_REGISTER(button, CONFIG_GPIO_LOG_LEVEL);

static const struct device *const buttons_dev =
	DEVICE_DT_GET(DT_NODELABEL(cros_buttons));

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
	case INPUT_KEY_POWER:
		handle_power_button(evt->value);
		break;
	case INPUT_KEY_VOLUMEUP:
		handle_volume_button(KEYBOARD_BUTTON_VOLUME_UP, evt->value);
		break;
	case INPUT_KEY_VOLUMEDOWN:
		handle_volume_button(KEYBOARD_BUTTON_VOLUME_DOWN, evt->value);
		break;
#ifdef CONFIG_PLATFORM_EC_DEDICATED_RECOVERY_BUTTON
	case INPUT_KEY_RECOVERY:
		handle_volume_button(KEYBOARD_BUTTON_RECOVERY, evt->value);
		break;
#endif
	default:
		LOG_DBG("Unknown button code=%u", evt->code);
		break;
	}
}
INPUT_CALLBACK_DEFINE(buttons_dev, buttons_cb_handler, NULL);

static void read_boot_button_state(void)
{
	boot_button_state = 0;

#if DT_NODE_EXISTS(DT_NODELABEL(vol_up))
	if (gpio_is_ready_dt(&vol_up_gpio)) {
		if (gpio_pin_get_dt(&vol_up_gpio) > 0) {
			boot_button_state |= BIT(BUTTON_VOLUME_UP);
		}
	}
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(vol_down))
	if (gpio_is_ready_dt(&vol_down_gpio)) {
		if (gpio_pin_get_dt(&vol_down_gpio) > 0) {
			boot_button_state |= BIT(BUTTON_VOLUME_DOWN);
		}
	}
#endif

	LOG_INF("Boot buttons: 0x%x", boot_button_state);
}

void button_init(void)
{
	/* Read initial button states before input subsystem starts */
	read_boot_button_state();

	/*
	 * Keep buttons device permanently active to prevent PM from suspending
	 * it during async event delivery, which would drop critical button
	 * events (power, volume) during low-power states (S0ix/S3).
	 */
	pm_device_runtime_get(buttons_dev);
}

uint32_t button_get_boot_button(void)
{
	return boot_button_state;
}

#ifdef CONFIG_PLATFORM_EC_CMD_BUTTON

/* Work item for deferred button release */
static struct k_work_delayable button_release_work;
static uint32_t simulated_button_mask;

/**
 * Simulate button release after a delay
 */
static void simulate_button_release(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Release all simulated buttons */
	if (simulated_button_mask & BIT(BUTTON_VOLUME_UP)) {
		input_report_key(buttons_dev, INPUT_KEY_VOLUMEUP, 0, true,
				 K_FOREVER);
	}
	if (simulated_button_mask & BIT(BUTTON_VOLUME_DOWN)) {
		input_report_key(buttons_dev, INPUT_KEY_VOLUMEDOWN, 0, true,
				 K_FOREVER);
	}
#ifdef CONFIG_PLATFORM_EC_DEDICATED_RECOVERY_BUTTON
	if (simulated_button_mask & BIT(BUTTON_RECOVERY)) {
		input_report_key(buttons_dev, INPUT_KEY_RECOVERY, 0, true,
				 K_FOREVER);
	}
#endif

	simulated_button_mask = 0;
	LOG_DBG("Button simulation released");
}

/**
 * Simulate button press and schedule release
 */
static void simulate_button(uint32_t button_mask, int press_ms)
{
	/* Cancel any pending release */
	k_work_cancel_delayable(&button_release_work);

	simulated_button_mask = button_mask;

	/* Press the buttons */
	if (button_mask & BIT(BUTTON_VOLUME_UP)) {
		input_report_key(buttons_dev, INPUT_KEY_VOLUMEUP, 1, true,
				 K_FOREVER);
		LOG_DBG("Volume Up pressed");
	}
	if (button_mask & BIT(BUTTON_VOLUME_DOWN)) {
		input_report_key(buttons_dev, INPUT_KEY_VOLUMEDOWN, 1, true,
				 K_FOREVER);
		LOG_DBG("Volume Down pressed");
	}
#ifdef CONFIG_PLATFORM_EC_DEDICATED_RECOVERY_BUTTON
	if (button_mask & BIT(BUTTON_RECOVERY)) {
		input_report_key(buttons_dev, INPUT_KEY_RECOVERY, 1, true,
				 K_FOREVER);
		LOG_DBG("Recovery pressed");
	}
#endif

	/* Schedule button release */
	k_work_schedule(&button_release_work, K_MSEC(press_ms));
}

static int command_button(const struct shell *sh, size_t argc, char **argv)
{
	int press_ms = 50;
	char *e;
	int argv_idx;
	uint32_t button_mask = 0;

	ARG_UNUSED(sh);

	if (argc < 2) {
		return EC_ERROR_PARAM_COUNT;
	}

	for (argv_idx = 1; argv_idx < argc; argv_idx++) {
		if (!strcasecmp(argv[argv_idx], "vup")) {
			button_mask |= BIT(BUTTON_VOLUME_UP);
		} else if (!strcasecmp(argv[argv_idx], "vdown")) {
			button_mask |= BIT(BUTTON_VOLUME_DOWN);
		} else if (!strcasecmp(argv[argv_idx], "rec")) {
#ifdef CONFIG_PLATFORM_EC_DEDICATED_RECOVERY_BUTTON
			button_mask |= BIT(BUTTON_RECOVERY);
#else
			/* Recovery button not supported without CONFIG */
			return EC_ERROR_PARAM1 + argv_idx - 1;
#endif
		} else {
			/* Check if last parameter is an integer (duration) */
			if (argv_idx == argc - 1) {
				press_ms = strtoi(argv[argv_idx], &e, 0);
				/* If valid integer, break out of loop */
				if (!*e) {
					break;
				}
			}
			/* Invalid button name */
			return EC_ERROR_PARAM1 + argv_idx - 1;
		}
	}

	if (!button_mask) {
		return EC_SUCCESS;
	}

	simulate_button(button_mask, press_ms);

	return EC_SUCCESS;
}

#ifdef CONFIG_PLATFORM_EC_DEDICATED_RECOVERY_BUTTON
SHELL_CMD_REGISTER(button, NULL, "Simulate button press (vup|vdown|rec [msec])",
		   command_button);
#else
SHELL_CMD_REGISTER(button, NULL, "Simulate button press (vup|vdown [msec])",
		   command_button);
#endif

/**
 * Initialize button simulation work item
 */
static int button_sim_init(void)
{
	k_work_init_delayable(&button_release_work, simulate_button_release);
	return 0;
}
SYS_INIT(button_sim_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_PLATFORM_EC_CMD_BUTTON */
