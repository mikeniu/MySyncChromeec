/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_suspend_resume, LOG_LEVEL_DBG);

#define DT_DRV_COMPAT cros_ec_gpio_suspend_resume

/*
 * Construct a gpio_dt_spec from a named-gpio phandle reference.
 * The ChromeOS EC DT stores full 32-bit flags in named-gpio nodes,
 * but gpio_dt_spec.dt_flags is only 8/16 bits. Mask to keep only the
 * operational flags (e.g., GPIO_ACTIVE_LOW).
 */
#define GPIO_SPEC_FROM_NAMED_GPIO(node, prop, idx)                             \
	{                                                                      \
		.port = DEVICE_DT_GET(DT_GPIO_CTLR(                            \
			DT_PHANDLE_BY_IDX(node, prop, idx), gpios)),           \
		.pin = DT_GPIO_PIN(DT_PHANDLE_BY_IDX(node, prop, idx), gpios), \
		.dt_flags = 0xFF &                                             \
			    DT_GPIO_FLAGS(DT_PHANDLE_BY_IDX(node, prop, idx),  \
					  gpios),                              \
	},

struct gpio_suspend_resume_data {
	const struct gpio_dt_spec *gpios;
	size_t num_gpios;
	struct ap_power_ev_callback cb;
};

static void gpio_suspend_resume_handler(struct ap_power_ev_callback *callback,
					struct ap_power_ev_data data)
{
	struct gpio_suspend_resume_data *inst =
		CONTAINER_OF(callback, struct gpio_suspend_resume_data, cb);
	int value;

	switch (data.event) {
	case AP_POWER_PRE_INIT:
	case AP_POWER_STARTUP:
		/* Assert GPIOs when AP is on. */
		value = 1;
		LOG_DBG("AP startup: asserting %zu GPIOs", inst->num_gpios);
		break;
	case AP_POWER_HARD_OFF:
		/* De-assert GPIOs when powered off. */
		value = 0;
		LOG_DBG("AP hard off: de-asserting %zu GPIOs", inst->num_gpios);
		break;
	default:
		return;
	}

	for (size_t i = 0; i < inst->num_gpios; i++) {
		LOG_DBG("  gpio[%zu] port=%s pin=%d set=%d", i,
			inst->gpios[i].port->name, inst->gpios[i].pin, value);
		gpio_pin_set_dt(&inst->gpios[i], value);
	}
}

static int gpio_suspend_resume_init(struct gpio_suspend_resume_data *inst)
{
	LOG_INF("Initializing gpio_suspend_resume with %zu GPIOs",
		inst->num_gpios);

	for (size_t i = 0; i < inst->num_gpios; i++) {
		if (!gpio_is_ready_dt(&inst->gpios[i])) {
			LOG_ERR("device %s not ready",
				inst->gpios[i].port->name);
			return -EINVAL;
		}
		LOG_DBG("  gpio[%zu] port=%s pin=%d ready", i,
			inst->gpios[i].port->name, inst->gpios[i].pin);
	}

	ap_power_ev_init_callback(&inst->cb, gpio_suspend_resume_handler,
				  AP_POWER_PRE_INIT | AP_POWER_STARTUP |
					  AP_POWER_HARD_OFF);
	ap_power_ev_add_callback(&inst->cb);
	LOG_INF("gpio_suspend_resume callback registered");

	return 0;
}

#define GPIO_SUSPEND_RESUME_DEFINE(n)                                \
	static const struct gpio_dt_spec gpio_sr_gpios_##n[] = {     \
		DT_INST_FOREACH_PROP_ELEM(n, gpio_list,              \
					  GPIO_SPEC_FROM_NAMED_GPIO) \
	};                                                           \
	static struct gpio_suspend_resume_data gpio_sr_data_##n = {  \
		.gpios = gpio_sr_gpios_##n,                          \
		.num_gpios = ARRAY_SIZE(gpio_sr_gpios_##n),          \
	};                                                           \
	static int gpio_sr_init_##n(void)                            \
	{                                                            \
		return gpio_suspend_resume_init(&gpio_sr_data_##n);  \
	}                                                            \
	SYS_INIT(gpio_sr_init_##n, APPLICATION,                      \
		 CONFIG_APPLICATION_INIT_PRIORITY);

DT_INST_FOREACH_STATUS_OKAY(GPIO_SUSPEND_RESUME_DEFINE)
