/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_switchcap_adc

#include "adc.h"
#ifndef CONFIG_AP_PWRSEQ_DRIVER
#include "power/qcom.h"
#endif

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_switchcap.h>

LOG_MODULE_REGISTER(switchcap_adc, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,switchcap-adc should be defined.");

struct switchcap_adc_config {
	struct gpio_dt_spec enable_gpio;
	enum adc_channel power_good_adc;
	int power_good_threshold_mv;
	int power_reset_threshold_mv;
	uint16_t power_off_delay_ms;
};

static const struct switchcap_adc_config switchcap_cfg = {
	.enable_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), enable_gpios),
	.power_good_adc = DT_STRING_UPPER_TOKEN(
		DT_INST_PHANDLE(0, power_good_adc), enum_name),
	.power_good_threshold_mv = DT_INST_PROP(0, power_good_threshold_mv),
	.power_reset_threshold_mv = DT_INST_PROP(0, power_reset_threshold_mv),
	.power_off_delay_ms = DT_INST_PROP_OR(0, power_off_delay_ms, 0),
};

int cros_switchcap_set_power(const struct device *dev, bool enable)
{
	const struct switchcap_adc_config *cfg = dev->config;
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

bool cros_switchcap_is_enabled(const struct device *dev)
{
	const struct switchcap_adc_config *cfg = dev->config;

	return gpio_pin_get_dt(&cfg->enable_gpio);
}

bool cros_switchcap_is_power_good(const struct device *dev)
{
	const struct switchcap_adc_config *cfg = dev->config;
	int adc_mv = adc_read_channel(cfg->power_good_adc);

	LOG_DBG("switchcap VPH power good ADC=%d mV", adc_mv);
	return adc_mv > cfg->power_good_threshold_mv;
}

bool cros_switchcap_is_power_reset(const struct device *dev)
{
	const struct switchcap_adc_config *cfg = dev->config;
	int adc_mv = adc_read_channel(cfg->power_good_adc);

	LOG_DBG("switchcap VPH reset ADC=%d mV", adc_mv);
	return adc_mv <= cfg->power_reset_threshold_mv;
}

#ifndef CONFIG_AP_PWRSEQ_DRIVER
void board_set_switchcap_power(int enable)
{
	(void)cros_switchcap_set_power(DEVICE_DT_INST_GET(0), enable != 0);
}

int board_is_switchcap_enabled(void)
{
	return cros_switchcap_is_enabled(DEVICE_DT_INST_GET(0));
}

int board_is_switchcap_power_good(void)
{
	return cros_switchcap_is_power_good(DEVICE_DT_INST_GET(0));
}

int board_is_switchcap_power_reset(void)
{
	return cros_switchcap_is_power_reset(DEVICE_DT_INST_GET(0));
}
#endif /* !CONFIG_AP_PWRSEQ_DRIVER */

static int switchcap_adc_init(const struct device *dev)
{
	const struct switchcap_adc_config *cfg = dev->config;
	int ret;

	if (!gpio_is_ready_dt(&cfg->enable_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->enable_gpio.port);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->enable_gpio, GPIO_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure enable GPIO: %d", ret);
		return ret;
	}

	return 0;
}

DEVICE_DT_INST_DEFINE(0, switchcap_adc_init, NULL, NULL, &switchcap_cfg,
		      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
