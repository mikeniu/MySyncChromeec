/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_switchcap

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_switchcap.h>

LOG_MODULE_REGISTER(switchcap, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,switchcap should be defined.");

struct switchcap_config {
	struct gpio_dt_spec enable_gpio;
	struct gpio_dt_spec power_good_gpio;
	int32_t poff_delay_ms;
};

static const struct switchcap_config switchcap_cfg = {
	.enable_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), enable_gpios),
	.power_good_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), power_good_gpios),
	.poff_delay_ms = DT_INST_PROP_OR(0, poff_delay_ms, 0),
};

int cros_switchcap_set_power(const struct device *dev, bool enable)
{
	const struct switchcap_config *cfg = dev->config;
	int ret;

	LOG_DBG("set_power(%d)", enable);
	ret = gpio_pin_set_dt(&cfg->enable_gpio, enable ? 1 : 0);
	if (ret < 0) {
		return ret;
	}

	if (!enable && cfg->poff_delay_ms > 0)
		k_msleep(cfg->poff_delay_ms);
	return 0;
}

bool cros_switchcap_is_enabled(const struct device *dev)
{
	const struct switchcap_config *cfg = dev->config;

	return gpio_pin_get_dt(&cfg->enable_gpio) == 1;
}

bool cros_switchcap_is_power_good(const struct device *dev)
{
	const struct switchcap_config *cfg = dev->config;

	return gpio_pin_get_dt(&cfg->power_good_gpio) == 1;
}

static int switchcap_init(const struct device *dev)
{
	const struct switchcap_config *cfg = dev->config;
	int ret;

	if (!gpio_is_ready_dt(&cfg->enable_gpio) ||
	    !gpio_is_ready_dt(&cfg->power_good_gpio)) {
		LOG_ERR("Switchcap GPIOs not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->enable_gpio, GPIO_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure enable GPIO: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&cfg->power_good_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure power-good GPIO: %d", ret);
		return ret;
	}

	LOG_INF("Switchcap driver initialized");
	return 0;
}

DEVICE_DT_INST_DEFINE(0, switchcap_init, NULL, NULL, &switchcap_cfg,
		      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
