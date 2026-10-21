/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/clock.h>
#include <zephyr/ztest.h>

#ifndef CONFIG_PM
#define CONFIG_PM 1
#endif
#undef CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT

#include <reg/reg_rtmr.h>
#include <reg/reg_system.h>
#include <reg/reg_timer.h>
#include <soc_clock.h>

/* Mock hardware register blocks */
static RTOSTMR_Type mock_rtmr;
static RTOSTMR_Type mock_dt_slwtmr0;
static SYSTEM_Type mock_dt_sccon;
static struct timer32_type mock_dt_timer5;
static uint32_t mock_sys_clock_cycles;

/* Redirect devicetree and interrupt primitives before including driver */
#undef DT_NUM_INST_STATUS_OKAY
#define DT_NUM_INST_STATUS_OKAY(compat) 1

#undef DT_INST_REG_ADDR
#define DT_INST_REG_ADDR(inst) ((uintptr_t)&mock_rtmr)

#undef DT_NODE_HAS_STATUS_OKAY
#define DT_NODE_HAS_STATUS_OKAY(node_id) 1

#undef DT_NODELABEL
#define DT_NODELABEL(label) mock_dt_##label

#undef DT_REG_ADDR
#define DT_REG_ADDR(node_id) ((uintptr_t)&(node_id))

#undef DT_INST_IRQN
#define DT_INST_IRQN(inst) 0

#undef DT_INST_IRQ
#define DT_INST_IRQ(inst, cell) 0

#undef IRQ_CONNECT
#define IRQ_CONNECT(...)

#undef irq_enable
#define irq_enable(irq)

#undef SYS_INIT
#define SYS_INIT(...)

static inline void NVIC_ClearPendingIRQ(int irq)
{
	ARG_UNUSED(irq);
}

/* Prevent system_timer_generic.h from redefining native_sim's sys_clock_* */
#define ZEPHYR_DRIVERS_TIMER_SYSTEM_TIMER_GENERIC_H_

static inline void timer_core_announce_from(k_spinlock_key_t key)
{
	ARG_UNUSED(key);
}

static inline void timer_core_init(void)
{
}

#define sys_clock_cycle_get_32 mock_sys_clock_cycle_get_32
#define sys_clock_set_timeout mock_sys_clock_set_timeout
#define sys_clock_idle_enter rts5912_test_sys_clock_idle_enter
#define sys_clock_idle_exit rts5912_test_sys_clock_idle_exit
#define sys_clock_disable rts5912_test_sys_clock_disable

static uint32_t mock_sys_clock_cycle_get_32(void)
{
	return mock_sys_clock_cycles;
}

static void mock_sys_clock_set_timeout(int32_t ticks, bool idle)
{
	ARG_UNUSED(ticks);
	ARG_UNUSED(idle);
}

/* Include the real RTS5912 RTMR driver implementation */
#include "realtek_rts5912_rtmr.c"

ZTEST(rts5912_rtmr, test_watchdog_compensation_heavy_sleep)
{
	/* Setup mock timer5 state: enabled, 1 second remaining at 25 MHz */
	mock_dt_timer5.ctrl = TIMER32_CTRL_EN;
	mock_dt_timer5.cnt = 25000000;
	mock_dt_timer5.ldcnt = 25000000;

	/* Enter heavy sleep at cycle 1000 */
	mock_sys_clock_cycles = 1000;
	cyc_sleep_compensated = 0;
	rts5912_clock_capture_low_freq_timer();

	/* Simulate 500 ms heavy sleep (32.768 kHz * 0.5 s = 16384 cycles) */
	mock_sys_clock_cycles = 1000 + 16384;
	rts5912_clock_compensate_system_timer();

	/* Expect 12.5M ticks deducted: 16384 * 25000000 / 32768 = 12500000 */
	zassert_equal(mock_dt_timer5.ldcnt, 25000000 - 12500000,
		      "Timer 5 was not compensated correctly: %u",
		      mock_dt_timer5.ldcnt);
	zassert_equal(cyc_sleep_compensated, 16384,
		      "Compensated cycle accumulator mismatch");

	/* Simulate long sleep that exceeds remaining timer5 count */
	mock_dt_timer5.cnt = 5000000; /* 200 ms remaining */
	mock_sys_clock_cycles = 20000;
	rts5912_clock_capture_low_freq_timer();

	mock_sys_clock_cycles = 20000 + 16384; /* 500 ms sleep */
	rts5912_clock_compensate_system_timer();

	/* Expect clamp to 1 so pre-warning interrupt fires immediately */
	zassert_equal(mock_dt_timer5.ldcnt, 1,
		      "Timer 5 should clamp to 1 when sleep exceeds remaining");
}

ZTEST_SUITE(rts5912_rtmr, NULL, NULL, NULL, NULL, NULL);
