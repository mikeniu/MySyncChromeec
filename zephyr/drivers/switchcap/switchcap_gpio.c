/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_switchcap

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#if defined(CONFIG_CROS_SWITCHCAP_PG_ADC)
#include <zephyr/drivers/adc.h>
#endif
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_switchcap.h>

LOG_MODULE_REGISTER(switchcap, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,switchcap should be defined.");

#if defined(CONFIG_CROS_SWITCHCAP_PG_GPIO)
BUILD_ASSERT(DT_INST_NODE_HAS_PROP(0, power_good_gpios),
	     "power-good-gpios property is required for GPIO power-good.");
BUILD_ASSERT(!DT_INST_NODE_HAS_PROP(0, io_channels),
	     "io-channels property must not be defined for GPIO power-good.");
#elif defined(CONFIG_CROS_SWITCHCAP_PG_ADC)
BUILD_ASSERT(DT_INST_NODE_HAS_PROP(0, io_channels),
	     "io-channels property is required for ADC power-good.");
BUILD_ASSERT(
	!DT_INST_NODE_HAS_PROP(0, power_good_gpios),
	"power-good-gpios property must not be defined for ADC power-good.");
#endif

struct switchcap_config {
	struct gpio_dt_spec enable_gpio;
#if defined(CONFIG_CROS_SWITCHCAP_PG_GPIO)
	struct gpio_dt_spec power_good_gpio;
#elif defined(CONFIG_CROS_SWITCHCAP_PG_ADC)
	struct adc_dt_spec power_good_adc;
	int32_t power_good_threshold_mv;
#endif
	uint16_t power_off_delay_ms;
};

static const struct switchcap_config switchcap_cfg = {
	.enable_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), enable_gpios),
#if defined(CONFIG_CROS_SWITCHCAP_PG_GPIO)
	.power_good_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), power_good_gpios),
#elif defined(CONFIG_CROS_SWITCHCAP_PG_ADC)
	.power_good_adc = ADC_DT_SPEC_INST_GET(0),
	.power_good_threshold_mv = DT_INST_PROP(0, power_good_threshold_mv),
#endif
	.power_off_delay_ms = DT_INST_PROP_OR(0, power_off_delay_ms, 0),
};

#if defined(CONFIG_CROS_SWITCHCAP_PG_ADC)
static int switchcap_read_adc_mv(const struct adc_dt_spec *adc, int32_t *val_mv)
{
	int16_t raw_buf;
	int ret;
	struct adc_sequence seq = {
		.buffer = &raw_buf,
		.buffer_size = sizeof(raw_buf),
	};

	ret = adc_sequence_init_dt(adc, &seq);
	if (ret < 0) {
		LOG_ERR("Failed to init ADC sequence: %d", ret);
		return ret;
	}

	ret = adc_read_dt(adc, &seq);
	if (ret < 0) {
		LOG_ERR("Failed to read power-good ADC: %d", ret);
		return ret;
	}

	*val_mv = raw_buf;
	ret = adc_raw_to_millivolts_dt(adc, val_mv);
	if (ret < 0) {
		LOG_ERR("Failed to convert ADC raw to mV: %d", ret);
		return ret;
	}

	LOG_DBG("switchcap power-good ADC=%d mV", *val_mv);
	return 0;
}
#endif

int cros_switchcap_set_power(const struct device *dev, bool enable)
{
	const struct switchcap_config *cfg = dev->config;
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
	const struct switchcap_config *cfg = dev->config;

	return gpio_pin_get_dt(&cfg->enable_gpio);
}

bool cros_switchcap_is_power_good(const struct device *dev)
{
	const struct switchcap_config *cfg = dev->config;

#if defined(CONFIG_CROS_SWITCHCAP_PG_GPIO)
	return gpio_pin_get_dt(&cfg->power_good_gpio);
#elif defined(CONFIG_CROS_SWITCHCAP_PG_ADC)
	int32_t val_mv;

	if (switchcap_read_adc_mv(&cfg->power_good_adc, &val_mv) < 0) {
		return false;
	}

	return val_mv > cfg->power_good_threshold_mv;
#endif
}

static int switchcap_init(const struct device *dev)
{
	const struct switchcap_config *cfg = dev->config;
	int ret;

	if (!gpio_is_ready_dt(&cfg->enable_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->enable_gpio.port);
		return -ENODEV;
	}

#if defined(CONFIG_CROS_SWITCHCAP_PG_GPIO)
	if (!gpio_is_ready_dt(&cfg->power_good_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->power_good_gpio.port);
		return -ENODEV;
	}
#elif defined(CONFIG_CROS_SWITCHCAP_PG_ADC)
	if (!adc_is_ready_dt(&cfg->power_good_adc)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->power_good_adc.dev);
		return -ENODEV;
	}
#endif

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

#if defined(CONFIG_CROS_SWITCHCAP_PG_GPIO)
	ret = gpio_pin_configure_dt(&cfg->power_good_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure power-good GPIO: %d", ret);
		return ret;
	}
#elif defined(CONFIG_CROS_SWITCHCAP_PG_ADC)
	ret = adc_channel_setup_dt(&cfg->power_good_adc);
	if (ret < 0) {
		LOG_ERR("Failed to setup power-good ADC channel: %d", ret);
		return ret;
	}
#endif

	return 0;
}

DEVICE_DT_INST_DEFINE(0, switchcap_init, NULL, NULL, &switchcap_cfg,
		      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
