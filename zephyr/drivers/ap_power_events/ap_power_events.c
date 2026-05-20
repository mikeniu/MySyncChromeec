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

LOG_MODULE_REGISTER(ap_power_events, LOG_LEVEL_DBG);

#define DT_DRV_COMPAT cros_ec_ap_power_events

struct ap_power_events_data {
	const struct gpio_dt_spec *gpios;
	size_t num_gpios;
	struct ap_power_ev_callback cb;
};

static void ap_power_events_handler(struct ap_power_ev_callback *callback,
				    struct ap_power_ev_data data)
{
	struct ap_power_events_data *inst =
		CONTAINER_OF(callback, struct ap_power_events_data, cb);
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
		gpio_pin_set_dt(&inst->gpios[i], value);
		LOG_DBG("  gpio[%zu] port=%s pin=%d state=%d ", i,
			inst->gpios[i].port->name, inst->gpios[i].pin,
			gpio_pin_get_dt(&inst->gpios[i]));
	}
}

static int ap_power_events_init(struct ap_power_events_data *inst)
{
	LOG_INF("Initializing ap_power_events with %zu GPIOs", inst->num_gpios);

	for (size_t i = 0; i < inst->num_gpios; i++) {
		if (!gpio_is_ready_dt(&inst->gpios[i])) {
			LOG_ERR("device %s not ready",
				inst->gpios[i].port->name);
			return -EINVAL;
		}
		gpio_pin_configure_dt(&inst->gpios[i], GPIO_OUTPUT_INACTIVE);
		LOG_DBG("  gpio[%zu] port=%s pin=%d ready", i,
			inst->gpios[i].port->name, inst->gpios[i].pin);
	}

	ap_power_ev_init_callback(&inst->cb, ap_power_events_handler,
				  AP_POWER_PRE_INIT | AP_POWER_STARTUP |
					  AP_POWER_HARD_OFF);
	ap_power_ev_add_callback(&inst->cb);
	LOG_INF("ap_power_events callback registered");

	return 0;
}

#define AP_POWER_EVENTS_DEFINE(n)                                            \
	static const struct gpio_dt_spec ap_pwr_gpios_##n[] = {              \
		DT_INST_FOREACH_PROP_ELEM_SEP(n, event_gpios,                \
					      GPIO_DT_SPEC_GET_BY_IDX, (, )) \
	};                                                                   \
	static struct ap_power_events_data ap_pwr_data_##n = {               \
		.gpios = ap_pwr_gpios_##n,                                   \
		.num_gpios = ARRAY_SIZE(ap_pwr_gpios_##n),                   \
	};                                                                   \
	static int ap_pwr_init_##n(void)                                     \
	{                                                                    \
		return ap_power_events_init(&ap_pwr_data_##n);               \
	}                                                                    \
	SYS_INIT(ap_pwr_init_##n, APPLICATION,                               \
		 CONFIG_APPLICATION_INIT_PRIORITY);

DT_INST_FOREACH_STATUS_OKAY(AP_POWER_EVENTS_DEFINE)
