/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/cros_system.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/state.h>
#include <zephyr/ztest.h>

#include <soc_clock.h>
#include <sys/mman.h>

FAKE_VOID_FUNC(watchdog_reload);
FAKE_VALUE_FUNC(uint64_t, rts5912_clock_get_sleep_ticks);

static const struct wdt_driver_api wdog_dummy_api;

DEVICE_DT_DEFINE(DT_NODELABEL(wdog), NULL, NULL, NULL, NULL, PRE_KERNEL_1, 1,
		 &wdog_dummy_api);

/* Map RTS5912 MMIO register windows before PRE_KERNEL_1 SYS_INIT runs. */
static int map_rtk_mmio_regs(void)
{
	void *p1 = mmap((void *)0x40010000, 0x4000, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	void *p2 = mmap((void *)0x40104000, 0x1000, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

	__ASSERT_NO_MSG(p1 != MAP_FAILED);
	__ASSERT_NO_MSG(p2 != MAP_FAILED);

	/* Mark OTP controller ready (OTP_STS_PDSTB_MSK = 0x04). */
	*(volatile uint32_t *)0x40010ea0 = 0x04;
	return 0;
}
SYS_INIT(map_rtk_mmio_regs, EARLY, 0);

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	k_cpu_idle();
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	irq_unlock(0);
}

static void rts5912_rtmr_before(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(watchdog_reload);
	RESET_FAKE(rts5912_clock_get_sleep_ticks);
}

ZTEST(rts5912_rtmr, test_watchdog_reload_on_suspend_to_idle)
{
	const struct pm_state_info s2idle = {
		.state = PM_STATE_SUSPEND_TO_IDLE,
	};
	const struct pm_state_info standby = {
		.state = PM_STATE_STANDBY,
	};

	zassert_true(pm_state_force(0U, &s2idle));
	k_sleep(K_MSEC(1));
	zassert_equal(watchdog_reload_fake.call_count, 1,
		      "Expected watchdog_reload on PM_STATE_SUSPEND_TO_IDLE");

	RESET_FAKE(watchdog_reload);
	zassert_true(pm_state_force(0U, &standby));
	k_sleep(K_MSEC(1));
	zassert_equal(watchdog_reload_fake.call_count, 0,
		      "Unexpected watchdog_reload on PM_STATE_STANDBY");
}

ZTEST_SUITE(rts5912_rtmr, NULL, NULL, rts5912_rtmr_before, NULL, NULL);
