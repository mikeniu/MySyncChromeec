/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_btn_ign

#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

#include <drivers/btn_ign.h>

LOG_MODULE_REGISTER(btn_ign, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "only one 'cros-ec,btn_ign' compatible node may be present");

struct btn_ign_config {
	struct gpio_dt_spec btn_ign_gpio;
};

static const struct btn_ign_config cfg = {
	.btn_ign_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), btn_ign_gpios),
};

static struct k_work_delayable delayed_deactivation;

static void delayed_deactivation_handler();

static int btn_ign_init(const struct device *dev)
{
	int ret;
	const struct btn_ign_config *cfg = dev->config;

	if (!gpio_is_ready_dt(&cfg->btn_ign_gpio)) {
		LOG_ERR("GPIO is not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->btn_ign_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Pin configuration failed: %d", ret);
		return ret;
	}

	k_work_init_delayable(&delayed_deactivation,
			      delayed_deactivation_handler);

	return 0;
}

void btn_ign_activate(void)
{
	k_work_cancel_delayable(&delayed_deactivation);
	gpio_pin_set_dt(&cfg.btn_ign_gpio, 1);
	LOG_INF("Power button ignore activated.");
}

void btn_ign_deactivate(void)
{
	k_work_schedule(&delayed_deactivation, K_SECONDS(2));
}

static void delayed_deactivation_handler()
{
	gpio_pin_set_dt(&cfg.btn_ign_gpio, 0);
	LOG_INF("Power button ignore deactivated.");
}

DEVICE_DT_INST_DEFINE(0, btn_ign_init, NULL, NULL, &cfg, POST_KERNEL,
		      CONFIG_GPIO_INIT_PRIORITY, NULL);
