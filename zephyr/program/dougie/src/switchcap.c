/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_switchcap

#include "adc.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_switchcap.h>

LOG_MODULE_REGISTER(dougie_switchcap, LOG_LEVEL_INF);

/*
 * Set the power-good threshold for VPH_PWR (mV).
 * Consistent with bluey/calypso PMC1010H VPH_PWR threshold.
 */
#define VPH_PWR_THRESHOLD_MV 2000

static const struct gpio_dt_spec enable_gpio =
	GPIO_DT_SPEC_GET(DT_DRV_INST(0), enable_gpios);

int cros_switchcap_set_power(const struct device *dev, bool enable)
{
	ARG_UNUSED(dev);

	return gpio_pin_set_dt(&enable_gpio, enable);
}

bool cros_switchcap_is_enabled(const struct device *dev)
{
	ARG_UNUSED(dev);

	return gpio_pin_get_dt(&enable_gpio);
}

bool cros_switchcap_is_power_good(const struct device *dev)
{
	int adc_value;

	ARG_UNUSED(dev);

	adc_value = adc_read_channel(ADC_VPH_PWR);
	LOG_DBG("switchcap VPH power good ADC value=%d mV", adc_value);

	return adc_value > VPH_PWR_THRESHOLD_MV;
}

static int switchcap_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (!gpio_is_ready_dt(&enable_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(enable_gpio.port);
		return -ENODEV;
	}

	return gpio_pin_configure_dt(&enable_gpio, GPIO_OUTPUT);
}

DEVICE_DT_INST_DEFINE(0, switchcap_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
