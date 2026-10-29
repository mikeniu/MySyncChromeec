/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_switchcap_comparator

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/comparator.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_switchcap.h>

LOG_MODULE_REGISTER(switchcap_comparator, LOG_LEVEL_INF);

struct switchcap_comp_config {
	struct gpio_dt_spec enable_gpio;
	const struct device *power_good_comp;
	uint16_t power_off_delay_ms;
};

static int switchcap_comp_set_power(const struct device *dev, bool enable)
{
	const struct switchcap_comp_config *cfg = dev->config;
	int ret;

	ret = gpio_pin_set_dt(&cfg->enable_gpio, enable);
	if (ret < 0) {
		return ret;
	}

	if (!enable && cfg->power_off_delay_ms > 0) {
		k_msleep(cfg->power_off_delay_ms);
	}

	return 0;
}

static bool switchcap_comp_is_enabled(const struct device *dev)
{
	const struct switchcap_comp_config *cfg = dev->config;

	return gpio_pin_get_dt(&cfg->enable_gpio);
}

static bool switchcap_comp_is_power_good(const struct device *dev)
{
	const struct switchcap_comp_config *cfg = dev->config;
	int ret;

	ret = comparator_get_output(cfg->power_good_comp);
	if (ret < 0) {
		LOG_ERR("Failed to read power-good comparator: %d", ret);
		return false;
	}

	return ret == 1;
}

static DEVICE_API(cros_switchcap, switchcap_comp_api) = {
	.set_power = switchcap_comp_set_power,
	.is_enabled = switchcap_comp_is_enabled,
	.is_power_good = switchcap_comp_is_power_good,
};

static int switchcap_comp_init(const struct device *dev)
{
	const struct switchcap_comp_config *cfg = dev->config;
	int ret;

	if (!gpio_is_ready_dt(&cfg->enable_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->enable_gpio.port);
		return -ENODEV;
	}

	if (!device_is_ready(cfg->power_good_comp)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->power_good_comp);
		return -ENODEV;
	}

	/*
	 * Configure as output without forcing an initial level so the existing
	 * hardware state is preserved until the initial power state is
	 * explicitly set.
	 */
	ret = gpio_pin_configure_dt(&cfg->enable_gpio, GPIO_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure enable GPIO: %d", ret);
		return ret;
	}

	return 0;
}

#define SWITCHCAP_COMP_INIT(inst)                                               \
	static const struct switchcap_comp_config switchcap_comp_cfg_##inst = { \
		.enable_gpio = GPIO_DT_SPEC_INST_GET(inst, enable_gpios),       \
		.power_good_comp =                                              \
			DEVICE_DT_GET(DT_INST_PHANDLE(inst, power_good_comp)),  \
		.power_off_delay_ms =                                           \
			DT_INST_PROP_OR(inst, power_off_delay_ms, 0),           \
	};                                                                      \
	DEVICE_DT_INST_DEFINE(inst, switchcap_comp_init, NULL, NULL,            \
			      &switchcap_comp_cfg_##inst, POST_KERNEL,          \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,               \
			      &switchcap_comp_api);

DT_INST_FOREACH_STATUS_OKAY(SWITCHCAP_COMP_INIT)
