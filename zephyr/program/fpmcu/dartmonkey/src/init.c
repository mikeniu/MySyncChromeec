/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>

static int init_fp_buffers(void)
{
	// uint8_t fp_template[FP_MAX_FINGER_COUNT]
	// 		   [FP_ALGORITHM_TEMPLATE_SIZE] FP_TEMPLATE_SECTION
	// 			   __aligned(4);
	// struct enc_buffer fp_enc_buffer FP_TEMPLATE_SECTION;
	// uint8_t fp_buffer[FP_SENSOR_IMAGE_SIZE] FP_FRAME_SECTION
	// __aligned(4);

	return 0;
}
SYS_INIT(init_fp_buffers, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

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

	return 0;
}
SYS_INIT(init_clock, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
