/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "init.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <common.h>
#include <gpio_signal.h>

LOG_MODULE_REGISTER(dartmonkey_init, LOG_LEVEL_INF);

PINCTRL_DT_DEFINE(DT_NODELABEL(spi4));

test_export_static bool checked_is_prod;
static bool is_board_prod;

int is_board_prod_hw(bool *is_prod)
{
	if (checked_is_prod) {
		*is_prod = is_board_prod;
		return 0;
	}

	checked_is_prod = true;

	const struct gpio_dt_spec *spi_sel = GPIO_DT_FROM_NODELABEL(fp_spi_sel);
	const struct gpio_dt_spec *highside =
		GPIO_DT_FROM_NODELABEL(div_highside);
	int ret;

	if (!gpio_is_ready_dt(spi_sel)) {
		LOG_ERR("SPI SEL GPIO device not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(highside)) {
		LOG_ERR("DIV HIGHSIDE GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(spi_sel, GPIO_INPUT | GPIO_PULL_UP);
	if (ret < 0) {
		return ret;
	}
	gpio_pin_set_dt(highside, 1);
	k_usleep(5);

	/* PE1 == 0 -> Dev board, PE1 == 1 -> Production Hardware */
	*is_prod = gpio_pin_get_dt(spi_sel);
	LOG_INF("Detected hardware: %s", *is_prod ? "Production" : "Dev board");

	/* Clean up: disable pull-up on spi_sel and clear highside */
	gpio_pin_configure_dt(spi_sel, GPIO_INPUT);
	gpio_pin_set_dt(highside, 0);

	return 0;
}

test_export_static int init_spi_select(void)
{
	static const struct pinctrl_dev_config *spi4_pcfg =
		PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(spi4));
	bool is_prod;
	int ret = is_board_prod_hw(&is_prod);

	if (ret < 0) {
		return ret;
	}

	if (is_prod) {
		const struct pinctrl_state *prod_state;
		ret = pinctrl_lookup_state(spi4_pcfg, PINCTRL_STATE_PROD,
					   &prod_state);
		if (0 == ret) {
			ret = pinctrl_apply_state_direct(spi4_pcfg, prod_state);
		}
	} else {
		/* The default has already been applied by the SPI driver */
	}

	return ret;
}
SYS_INIT(init_spi_select, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
