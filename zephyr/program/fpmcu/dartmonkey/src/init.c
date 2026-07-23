/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <common.h>
#include <gpio_signal.h>
#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>

LOG_MODULE_REGISTER(dartmonkey_init, LOG_LEVEL_INF);

PINCTRL_DT_DEFINE(DT_NODELABEL(spi4));

test_export_static int init_spi_select(void)
{
	const struct gpio_dt_spec *spi_sel = GPIO_DT_FROM_NODELABEL(fp_spi_sel);
	const struct gpio_dt_spec *highside =
		GPIO_DT_FROM_NODELABEL(div_highside);
	static const struct pinctrl_dev_config *spi4_pcfg =
		PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(spi4));
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
	bool is_prod = gpio_pin_get_dt(spi_sel);
	LOG_INF("Detected hardware: %s", is_prod ? "Production" : "Dev board");

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

	/* Clean up: disable pull-up on spi_sel and clear highside */
	gpio_pin_configure_dt(spi_sel, GPIO_INPUT);
	gpio_pin_set_dt(highside, 0);

	return ret;
}
SYS_INIT(init_spi_select, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

static int init_clock(void)
{
	/*
	 * In soc.dtsi, &pll has status = "okay". Because of that, Zephyr's
	 * upstream clock driver, clock_stm32_ll_h7.c, enables PLL1 at boot and
	 * leaves the regulator at VOS0.
	 *
	 * Disable the PLL until clock.c explicitly requests it and override the
	 * regulator to VOS3 (lowest power state).
	 */
	LL_RCC_PLL1_Disable();
	LL_RCC_PLL1Q_Disable();
	LL_RCC_PLL1R_Disable();

	/*
	 * When not in fast CPU mode, all clocks run at <= 64 MHz. Switch from
	 * Zephyr's boot default regulator mode (VOS0) to VOS3 to save power.
	 * STM32H7 Reference Manual RM0433, Table 60 (Kernel clock distribution
	 * overview) confirms it's safe to do so:
	 *
	 *     SYSCLK: 64 MHz (HSI) ≤200  MHz (VOS3 limit)
	 *     AHB Bus: 64 MHz ≤100  MHz (VOS3 limit)
	 *     APB Buses (1–4): 64 MHz ≤70  MHz (VOS3 limit)
	 *     USART1 kernel clock: 64 MHz ≤70  MHz (VOS3 limit)
	 *     SPI1 & SPI4 kernel clocks: 64 MHz ≤70  MHz (VOS3 limit)
	 *     RNG kernel clock: 48 MHz ≤48  MHz (VOS3 limit)
	 */
	LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE3);
	while (LL_PWR_IsActiveFlag_VOS() == 0) {
	}

	return 0;
}
SYS_INIT(init_clock, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
