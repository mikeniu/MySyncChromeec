/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>

#include <gpio_signal.h>

#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>

LOG_MODULE_REGISTER(dartmonkey_init, LOG_LEVEL_INF);

PINCTRL_DT_DEFINE(DT_NODELABEL(spi4));

static int init_spi_select(void)
{
	const struct gpio_dt_spec *spi_sel =
		GPIO_DT_FROM_NODELABEL(fp_spi_sel);
	const struct gpio_dt_spec *highside =
		GPIO_DT_FROM_NODELABEL(div_highside);
	static const struct pinctrl_dev_config *spi4_pcfg =
		PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(spi4));
	int ret;

	if (!gpio_is_ready_dt(spi_sel)) {
		LOG_ERR("SPI SEL GPIO device not ready");
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
		ret = pinctrl_lookup_state(
			spi4_pcfg, PINCTRL_STATE_PROD, &prod_state);
		if (0 == ret) {
			ret = pinctrl_apply_state_direct(spi4_pcfg, prod_state);
		}
	}

	/* Clean up: disable pull-up on spi_sel and clear highside */
	gpio_pin_configure_dt(spi_sel, GPIO_INPUT);
	gpio_pin_set_dt(highside, 0);

	return ret;
}
SYS_INIT(init_spi_select, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

// TODO pre-devices?
static int init_clock(void)
{
	/* PLL is enabled in DTS to configure it by the STM clock driver. ...
	 * speed up CPU, switch sysclk*/
	LL_RCC_PLL1_Disable();
	// TODO disable pll before?
	LL_RCC_PLL1Q_Disable();
	LL_RCC_PLL1R_Disable();

	// Table 60. Kernel clock distribution overview - max freq for VSO
	LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE3);

	/* Console. */
	// TODO add second clock to uart dts? STM32_UART_DOMAIN_CLOCK_SUPPORT.
	// To get a proper source clock Current version will work only for APB
	// prescalers = 1 and SYSCLK = HSI
	//	LL_RCC_SetUSARTClockSource(LL_RCC_USART16_CLKSOURCE_HSI);

	// #define USART16_SEL(val)	STM32_CLOCK(val, 7, 3, D2CCIP2R_REG)

	/* SPI4 for sensor. */
	// LL_RCC_SetSPIClockSource(LL_RCC_SPI45_CLKSOURCE_HSI);
	/* Set HSI as source for per_ck, which is used for SPI1. */
	// Change to #define USART16_SEL(val)	STM32_CLOCK(val, 7, 3,
	// D2CCIP2R_REG)

	//	LL_RCC_SetCLKPClockSource(LL_RCC_CLKP_CLKSOURCE_HSI);
	/* SPI1 for Host Commands. */
	//	LL_RCC_SetSPIClockSource(LL_RCC_SPI123_CLKSOURCE_CLKP);

	// TODO Check other peri - SPI, RNG.
	// TODO Reconfigure timers.

	// TODO: should the following call be added here?
	// LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_SRAM1 |
	//                          LL_AHB2_GRP1_PERIPH_SRAM2 |
	//                          LL_AHB2_GRP1_PERIPH_SRAM3);

	return 0;
}
SYS_INIT(init_clock, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
