/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Define the devicetree compatible string for this driver.
 * This must match the "compatible" property in the DTS file
 * (e.g., "cros-ec,wot").
 */
#define DT_DRV_COMPAT cros_ec_wot

#include "power_button.h"
#include "tablet_mode.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(wot, LOG_LEVEL_INF);

#define TS_WOT_DELAY_TIME K_MSEC(100)
#define BL_DEBOUNCE_TIME K_MSEC(20)
#define PWR_BTN_DURATION 200

/**
 * struct wot_config - Hardware configuration structure.
 */
struct wot_config {
	struct gpio_dt_spec report_en;
	struct gpio_dt_spec wot_l;
	struct gpio_dt_spec bl_detect;
	struct gpio_dt_spec tchscr_int;
};

/**
 * struct wot_data - Runtime state structure.
 */
struct wot_data {
	bool wake_event_fired;
	struct gpio_callback bl_detect_cb;
	struct gpio_callback tchscr_int_cb;
};

static const struct device *wot_dev;

/**
 * Zephyr Workqueue task to simulate a power button press.
 */
static void wot_trigger_wake_worker(struct k_work *work)
{
	LOG_INF("WOT: Workqueue executing power button press");
	power_button_simulate_press(PWR_BTN_DURATION);
}
K_WORK_DEFINE(wot_trigger_wake_work, wot_trigger_wake_worker);

/**
 * Zephyr Delayable Workqueue task for backlight debounce.
 */
static void wot_backlight_debounce_worker(struct k_work *work)
{
	const struct wot_config *config = wot_dev->config;
	struct wot_data *data = wot_dev->data;

	if (gpio_pin_get_dt(&config->bl_detect)) {
		LOG_INF("WOT: Normal Mode (AP awake)");

		gpio_pin_interrupt_configure_dt(&config->tchscr_int,
						GPIO_INT_DISABLE);

		gpio_pin_set_dt(&config->report_en, 1);
		gpio_pin_set_dt(&config->wot_l, 0);
	} else {
		bool is_tablet = tablet_get_mode();
		data->wake_event_fired = false;

		if (is_tablet) {
			LOG_INF("WOT: Tablet Sleep Mode (WOT Active)");

			gpio_pin_set_dt(&config->report_en, 1);
			gpio_pin_set_dt(&config->wot_l, 1);
			gpio_pin_interrupt_configure_dt(&config->tchscr_int,
							GPIO_INT_EDGE_FALLING);
		} else {
			LOG_INF("WOT: Clamshell Sleep Mode (WOT Disabled)");

			gpio_pin_interrupt_configure_dt(&config->tchscr_int,
							GPIO_INT_DISABLE);

			gpio_pin_set_dt(&config->report_en, 0);
			gpio_pin_set_dt(&config->wot_l, 0);
		}
	}
}
K_WORK_DELAYABLE_DEFINE(wot_backlight_debounce_work,
			wot_backlight_debounce_worker);

/**
 * Backlight interrupt service routine.
 */
static void wot_bl_detect_isr(const struct device *port,
			      struct gpio_callback *cb, gpio_port_pins_t pins)
{
	k_work_reschedule(&wot_backlight_debounce_work, BL_DEBOUNCE_TIME);
	LOG_INF("WOT: Backlight interrupt triggered");
}

/**
 * Touch screen interrupt service routine.
 */
static void wot_tchscr_int_isr(const struct device *port,
			       struct gpio_callback *cb, gpio_port_pins_t pins)
{
	struct wot_data *data = wot_dev->data;
	const struct wot_config *config = wot_dev->config;

	/* Ignore touches if the backlight is already ON (system is awake) */
	if (gpio_pin_get_dt(&config->bl_detect))
		return;

	/* If we already fired a wake event, ignore subsequent touches */
	if (data->wake_event_fired)
		return;

	LOG_INF("WOT: Touch INT triggered");

	/* Lock the wake event flag to prevent duplicate power button presses */
	data->wake_event_fired = true;

	k_work_submit(&wot_trigger_wake_work);
}

/**
 * Device initialization function.
 */
static int wot_init(const struct device *dev)
{
	const struct wot_config *config = dev->config;
	struct wot_data *data = dev->data;

	wot_dev = dev;

	data->wake_event_fired = false;

	if (!gpio_is_ready_dt(&config->report_en))
		return -ENODEV;
	gpio_pin_configure_dt(&config->report_en, GPIO_OUTPUT_INACTIVE);

	if (!gpio_is_ready_dt(&config->wot_l))
		return -ENODEV;
	gpio_pin_configure_dt(&config->wot_l, GPIO_OUTPUT_INACTIVE);

	/*
	 * Verify, configure, and register the Touch Screen interrupt.
	 */
	if (!gpio_is_ready_dt(&config->tchscr_int))
		return -ENODEV;
	gpio_pin_configure_dt(&config->tchscr_int, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&config->tchscr_int, GPIO_INT_EDGE_FALLING);
	gpio_init_callback(&data->tchscr_int_cb, wot_tchscr_int_isr,
			   BIT(config->tchscr_int.pin));
	gpio_add_callback(config->tchscr_int.port, &data->tchscr_int_cb);

	/*
	 * Verify, configure, and register the Backlight interrupt.
	 */
	if (!gpio_is_ready_dt(&config->bl_detect))
		return -ENODEV;
	gpio_pin_configure_dt(&config->bl_detect, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&config->bl_detect, GPIO_INT_EDGE_BOTH);
	gpio_init_callback(&data->bl_detect_cb, wot_bl_detect_isr,
			   BIT(config->bl_detect.pin));
	gpio_add_callback(config->bl_detect.port, &data->bl_detect_cb);

	LOG_INF("WOT driver initialized successfully");
	return 0;
}

/**
 * WOT_INIT - Macro to instantiate a WOT device.
 */
#define WOT_INIT(inst)                                                       \
	static const struct wot_config wot_cfg_##inst = {                    \
		.report_en = GPIO_DT_SPEC_INST_GET(inst, report_en_gpios),   \
		.wot_l = GPIO_DT_SPEC_INST_GET(inst, wot_l_gpios),           \
		.bl_detect = GPIO_DT_SPEC_INST_GET(inst, bl_detect_gpios),   \
		.tchscr_int = GPIO_DT_SPEC_INST_GET(inst, tchscr_int_gpios), \
	};                                                                   \
	static struct wot_data wot_data_##inst;                              \
	DEVICE_DT_INST_DEFINE(inst, wot_init, NULL, &wot_data_##inst,        \
			      &wot_cfg_##inst, POST_KERNEL,                  \
			      CONFIG_APPLICATION_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(WOT_INIT)