/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "registers.h"
#include "util.h"

/* use 16Mhz High-speed oscillator */
#define HSI16_CLOCK 16000000

int clock_get_freq(void)
{
	return HSI16_CLOCK;
}

void clock_enable_module(enum module_id module, int enable)
{
}

/*
 * system closk is HSI16 = 16MHz,
 * no prescaler, no MCO, no PLL
 * USB clock = HSI48
 */
BUILD_ASSERT(CPU_CLOCK == HSI16_CLOCK);

void clock_init(void)
{
	/*
	 * The initial state :
	 *  SYSCLK from MSI (=2.097MHz), no divider on AHB, APB1, APB2
	 *  PLL unlocked
	 */

	/* put 1 Wait-State for flash access to ensure proper reads at 48Mhz */
	/*STM32_FLASH_ACR = 0x1001;*/ /* 1 WS / Prefetch enabled */

	/* Ensure that HSI16 is ON */
	if (!(STM32_RCC_CR & (1 << 2))) {
		/* Enable HSI16 */
		STM32_RCC_CR |= 1 << 0;
		/* Wait for HSI16 to be ready */
		while (!(STM32_RCC_CR & (1 << 2)))
			;
	}
	/* switch SYSCLK to HSI16 */
	STM32_RCC_CFGR = 0x00000001;

	/* wait until the HSI16 is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x4)
		;
}
