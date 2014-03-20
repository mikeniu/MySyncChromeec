/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* L053 discovery board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 16 MHz SYSCLK clock frequency */
#define CPU_CLOCK 16000000

/* the UART console is on USART2 (PA9/PA10) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* Optional features */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING
#undef CONFIG_ADC

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK_MSB 21
#define TIM_CLOCK_LSB 22

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_USER_BUTTON = 0,
	/* Unimplemented signals we emulate */
	GPIO_ENTERING_RW,
	GPIO_WP_L,
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

/* ADC signal */
enum adc_channel {
	/* Number of ADC channels */
	ADC_CH_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
