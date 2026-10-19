/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific hwtimer module for Chrome EC */

#ifndef __CROS_EC_HWTIMER_CHIP_H
#define __CROS_EC_HWTIMER_CHIP_H

#include <stdint.h>

/* Use ITIM32 as main hardware timer */
#define TICK_ITIM32_MAX_CNT 0xFFFFFFFF
/* Maximum deadline of event */
#define EVT_MAX_EXPIRED_US TICK_ITIM32_MAX_CNT
/* Max consecutive-read retries for 32.768 kHz clock-domain sync stability */
#define NPCX_HWTIMER_MAX_RETRIES 8

static inline uint16_t npcx_itim_read_stable16(uint16_t (*read_reg)(void))
{
	uint16_t cnt = read_reg();
	uint16_t cnt2;

	for (int i = 0;
	     i < NPCX_HWTIMER_MAX_RETRIES && (cnt2 = read_reg()) != cnt; i++)
		cnt = cnt2;

	return cnt;
}

static inline uint32_t npcx_itim_read_stable32(uint32_t (*read_reg)(void))
{
	uint32_t cnt = read_reg();
	uint32_t cnt2;

	for (int i = 0;
	     i < NPCX_HWTIMER_MAX_RETRIES && (cnt2 = read_reg()) != cnt; i++)
		cnt = cnt2;

	return cnt;
}

/* Clock source for ITIM16 */
enum ITIM_SOURCE_CLOCK_T {
	ITIM_SOURCE_CLOCK_APB2 = 0,
	ITIM_SOURCE_CLOCK_32K = 1,
};

/**
 * Initialise a hardware timer
 *
 * Select the source clock for a timer and prepare it for use.
 *
 * @param itim_no	Timer number to init (enum ITIM_MODULE_T)
 * @param source	Source for timer clock (enum ITIM_SOURCE_CLOCK_T)
 */
void init_hw_timer(int itim_no, enum ITIM_SOURCE_CLOCK_T source);

/* Returns the counter value of event timer */
uint16_t __hw_clock_event_count(void);

/* Returns time delay because of deep idle */
uint32_t __hw_clock_get_sleep_time(uint16_t pre_evt_cnt);

/* Handle ITIM32 overflow if interrupt is disabled */
void __hw_clock_handle_overflow(uint32_t clksrc_high);

/**
 * Set up the timer for use before the task system is available
 *
 * @param start_t	Value to assign to the counter
 */
void __hw_early_init_hwtimer(uint32_t start_t);

#endif /* __CROS_EC_HWTIMER_CHIP_H */
