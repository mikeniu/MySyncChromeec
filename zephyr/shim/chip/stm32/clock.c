/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"

#ifdef CONFIG_SOC_SERIES_STM32H7X
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/pm/policy.h>

#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>
#include <stm32_ll_usart.h>
#include <stm32_ll_utils.h>

#define PLLSRC_FREQ ((1ULL * STM32_HSI_FREQ) / (STM32_HSI_DIVISOR))
#define PLL_FREQ                                      \
	(((PLLSRC_FREQ) * (STM32_PLL_N_MULTIPLIER)) / \
	 ((STM32_PLL_M_DIVISOR) * (STM32_PLL_P_DIVISOR)))

#define RCC_AHB_DIV_(v) LL_RCC_AHB_DIV_##v
#define RCC_AHB_PRESCALER(v) RCC_AHB_DIV_(v)

#define PLL_RCC_AHB_DIV_2 2

test_export_static atomic_t fast_cpu_enabled = ATOMIC_INIT(0);

test_mockable void clock_enable_module(enum module_id module, int enable)
{
	if (module != MODULE_FAST_CPU) {
		return;
	}

	bool changed = atomic_test_and_set_bit_to(&fast_cpu_enabled, 0, enable);
	if (!changed) {
		return;
	}

	if (enable) {
		/*
		 * 1) scale voltage to VOS1
		 * 2) enable PLL1
		 * 3) set AHB prescaler and APB prescalers
		 * 4) increase flash latency
		 * 5) switch SYSCLK to PLL1
		 */

		pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);

		/*
		 * Do _not_ lock RCC via a hardware semaphore. It's only needed
		 * on dual-core STM32H7 and Dartmonkey runs on the single-core
		 * STM32H743VI.
		 */

		LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
		while (LL_PWR_IsActiveFlag_VOS() == 0) {
		}

		LL_RCC_PLL1_Enable();
		while (LL_RCC_PLL1_IsReady() != 1U) {
		}

		/* Set HPRE to 2 so that the AHB frequency is 200 MHz, the
		 * maximum allowed frequency according to STM32H7 Reference
		 * Manual RM0433, Table 60. */
		LL_RCC_SetAHBPrescaler(RCC_AHB_PRESCALER(PLL_RCC_AHB_DIV_2));

		/* APB prescalers are set to 1, resulting in 200 MHz at VOS1.
		 * According to RM0433, Table 60, the maximum allowed APB
		 * frequency at VOS1 is 120 MHz. Set all APB prescalers to 2.*/
		LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
		LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);
		LL_RCC_SetAPB3Prescaler(LL_RCC_APB3_DIV_2);
		LL_RCC_SetAPB4Prescaler(LL_RCC_APB4_DIV_2);

		LL_SetFlashLatency(PLL_FREQ / PLL_RCC_AHB_DIV_2);

		/* Set PLL1 as System Clock Source */
		LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);
		while (LL_RCC_GetSysClkSource() !=
		       LL_RCC_SYS_CLKSOURCE_STATUS_PLL1) {
		}

		if (IS_ENABLED(CONFIG_SYSTEM_CLOCK_HW_CYCLES_PER_SEC_RUNTIME_UPDATE)) {
			z_sys_clock_hw_cycles_per_sec_update(PLL_FREQ);
		}

		LL_SetSystemCoreClock(PLL_FREQ);
	} else {
		/*
		 * 1) switch SYSCLK to HSI
		 * 2) set AHB prescaler and APB prescalers
		 * 3) decrease flash latency
		 * 4) disable PLL1
		 * 5) scale voltage down to VOS3
		 */

		/* Enable HSI if not enabled */
		if (LL_RCC_HSI_IsReady() != 1) {
			/* Enable HSI */
			LL_RCC_HSI_Enable();
			while (LL_RCC_HSI_IsReady() != 1) {
				/* Wait for HSI ready */
			}
		}

		LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
		while (LL_RCC_GetSysClkSource() !=
		       LL_RCC_SYS_CLKSOURCE_STATUS_HSI) {
		}

		LL_RCC_SetAHBPrescaler(RCC_AHB_PRESCALER(STM32_HPRE));

		LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
		LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
		LL_RCC_SetAPB3Prescaler(LL_RCC_APB3_DIV_1);
		LL_RCC_SetAPB4Prescaler(LL_RCC_APB4_DIV_1);

		LL_SetFlashLatency(STM32_HSI_FREQ / STM32_HPRE);

		LL_RCC_PLL1_Disable();

		LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE3);
		while (LL_PWR_IsActiveFlag_VOS() == 0) {
		}

		if (IS_ENABLED(CONFIG_SYSTEM_CLOCK_HW_CYCLES_PER_SEC_RUNTIME_UPDATE)) {
			z_sys_clock_hw_cycles_per_sec_update(STM32_HSI_FREQ);
		}

		LL_SetSystemCoreClock(STM32_HSI_FREQ);

		pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
	}
}

#else
test_mockable void clock_enable_module(enum module_id module, int enable)
{
}
#endif
