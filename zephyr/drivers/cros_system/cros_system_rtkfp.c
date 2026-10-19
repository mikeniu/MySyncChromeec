/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>
#include "common.h"
#include "cros_version.h"
#include "drivers/cros_system.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/cache.h>
#include <zephyr/sys/sys_io.h>

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

/*
 * Exported for the unit tests in zephyr/test/drivers/cros_system_rtkfp,
 * which forward declare them instead of pulling them into a public header.
 */
uint8_t cros_system_rtkfp_get_chip_version(uint32_t sys_sts);
int cros_system_rtkfp_determine_reset_cause(uint32_t reg_dummy0);
int cros_system_rtkfp_init(void);

#ifndef CONFIG_SOC_RTS5817
/*
 * When built for a host unit test (native_sim) there is no RTS5817
 * register block, so back the registers with RAM.  This lets the reset
 * cause logic run and be inspected without hardware.
 */
uint32_t cros_system_rtkfp_test_r_chip_reset;
uint32_t cros_system_rtkfp_test_r_al_dummy0;
uint32_t cros_system_rtkfp_test_r_sys_sts;

#define R_CHIP_RESET ((uintptr_t)&cros_system_rtkfp_test_r_chip_reset)
#define R_AL_DUMMY0 ((uintptr_t)&cros_system_rtkfp_test_r_al_dummy0)
#define R_SYS_STS ((uintptr_t)&cros_system_rtkfp_test_r_sys_sts)
#else
#define R_CHIP_RESET 0x401e2100
#define R_AL_DUMMY0 0x401E2068
#define R_SYS_STS 0x40100000
#endif

#define CHIP_RESET_BIT BIT(0)
#define SYSTEM_RUN_FLAG BIT(16)
#define VER_NUM_OFFSET 16
#define VER_NUM_MASK (0xFF << VER_NUM_OFFSET)

static int reset_cause = UNKNOWN_RST;

static const struct device *const watchdog =
	DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog));

static void rtkfp_resetchip(void)
{
	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable_all();

	wdt_disable(watchdog);
	sys_clock_disable();

	/* Flush data cache */
	sys_cache_data_flush_all();

	barrier_dsync_fence_full();
	barrier_isync_fence_full();

	/* Chip reset */
	sys_set_bits(R_CHIP_RESET, CHIP_RESET_BIT);

	/* Spin and wait for reboot */
	while (1)
		;
}

const char *cros_system_chip_vendor(void)
{
	return "rtk";
}

const char *cros_system_chip_name(void)
{
	return "rts5817";
}

uint8_t cros_system_rtkfp_get_chip_version(uint32_t sys_sts)
{
	return FIELD_GET(VER_NUM_MASK, sys_sts);
}

const char *cros_system_chip_revision(void)
{
	static char buf[5];
	static bool init;

	if (!init) {
		snprintf(buf, sizeof(buf), "%u",
			 cros_system_rtkfp_get_chip_version(
				 sys_read32(R_SYS_STS)));
		init = true;
	}

	return buf;
}

int cros_system_rtkfp_determine_reset_cause(uint32_t reg_dummy0)
{
	/* The R_AL_DUMMY0 register won't be reset by watchdog or
	 * system reset, so we can know the reset cause via this register */
	if (reg_dummy0 & SYSTEM_RUN_FLAG)
		return WATCHDOG_RST;

	return POWERUP;
}

int cros_system_get_reset_cause(void)
{
	return reset_cause;
}

int cros_system_rtkfp_init(void)
{
	uint32_t reg_dummy0 = sys_read32(R_AL_DUMMY0);

	reset_cause = cros_system_rtkfp_determine_reset_cause(reg_dummy0);

	if (reset_cause == POWERUP)
		sys_set_bits(R_AL_DUMMY0, SYSTEM_RUN_FLAG);

	return 0;
}

int cros_system_soc_reset(void)
{
	rtkfp_resetchip();

	/* Should never return */
	return 0;
}

int cros_system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	return 0;
}

SYS_INIT(cros_system_rtkfp_init, PRE_KERNEL_1, CONFIG_CROS_SYSTEM_INIT_PRIORITY);

#if CONFIG_CROS_SYSTEM_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif
