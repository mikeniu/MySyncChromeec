/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"

#ifdef CONFIG_SOC_SERIES_STM32H7X
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>
#include <stm32_ll_usart.h>
#include <stm32_ll_utils.h>

extern unsigned int z_clock_hw_cycles_per_sec;

// build_assert(defined(STM32_PLL_SRC_HSI))
#define PLLSRC_FREQ ((1ULL * STM32_HSI_FREQ) / (STM32_HSI_DIVISOR))
#define PLL_FREQ                                      \
	(((PLLSRC_FREQ) * (STM32_PLL_N_MULTIPLIER)) / \
	 ((STM32_PLL_M_DIVISOR) * (STM32_PLL_P_DIVISOR)))

#define RCC_AHB_DIV_(v) LL_RCC_AHB_DIV_##v
#define RCC_AHB_DIV(v) RCC_AHB_DIV_(v)

#define PLL_RCC_AHB_DIV 2

test_mockable void clock_enable_module(enum module_id module, int enable)
{
	if (module == MODULE_FAST_CPU) {
		if (enable) {
			// TODO add hsem?
			// z_stm32_hsem_lock(CFG_HW_RCC_SEMID,
			// HSEM_LOCK_DEFAULT_RETRY);

			LL_PWR_SetRegulVoltageScaling(
				LL_PWR_REGU_VOLTAGE_SCALE1);
			while (LL_PWR_IsActiveFlag_VOS() == 0) {
			};

			LL_RCC_PLL1_Enable();
			while (LL_RCC_PLL1_IsReady() != 1U) {
			}

			/* Set HPRE to 2 not to extend AHB max freq. Also update
			 * the flash latency acording to the new AHB freq.
			 */
			LL_RCC_SetAHBPrescaler(RCC_AHB_DIV(PLL_RCC_AHB_DIV));
			// TODO assert na max ahb?

			// TODO limit APB freq not to extend 120MHz max and
			// restore from DTS?
			//&rcc {
			//	/* TODO check APB1-4 enr, if enabled - rather
			// yes */ 	d1ppre = <2>; 	d2ppre1 = <2>; 	d2ppre2
			// = <2>; 	d3ppre = <2>;
			//};
			//

			LL_SetFlashLatency(PLL_FREQ / PLL_RCC_AHB_DIV);

			/* Set PLL1 as System Clock Source */
			LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);
			while (LL_RCC_GetSysClkSource() !=
			       LL_RCC_SYS_CLKSOURCE_STATUS_PLL1) {
			}

			if (IS_ENABLED(
				    CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME)) {
				z_clock_hw_cycles_per_sec = PLL_FREQ;
			}
			SystemCoreClock = PLL_FREQ;
		} else {
			/* Enable HSI if not enabled */
			if (LL_RCC_HSI_IsReady() != 1) {
				/* Enable HSI */
				LL_RCC_HSI_Enable();
				while (LL_RCC_HSI_IsReady() != 1) {
					/* Wait for HSI ready */
				}
			}

			/* Set HSI as SYSCLCK source */
			LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
			while (LL_RCC_GetSysClkSource() !=
			       LL_RCC_SYS_CLKSOURCE_STATUS_HSI) {
			}

			/* Set HPRE to the original value and update flash
			 * latency. */
			LL_RCC_SetAHBPrescaler(RCC_AHB_DIV(STM32_HPRE));
			LL_SetFlashLatency(STM32_HSI_FREQ / STM32_HPRE);

			LL_RCC_PLL1_Disable();

			LL_PWR_SetRegulVoltageScaling(
				LL_PWR_REGU_VOLTAGE_SCALE3);
			while (LL_PWR_IsActiveFlag_VOS() == 0) {
			};

			if (IS_ENABLED(
				    CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME)) {
				z_clock_hw_cycles_per_sec = STM32_HSI_FREQ;
			}
			SystemCoreClock = STM32_HSI_FREQ;
		}
	}
}

#else
test_mockable void clock_enable_module(enum module_id module, int enable)
{
}
#endif
