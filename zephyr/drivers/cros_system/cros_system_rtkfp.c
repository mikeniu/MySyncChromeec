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

static int reset_cause = UNKNOWN_RST;

static const struct device *const watchdog =
	DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog));

#define R_CHIP_RESET 0x401e2100
#define CHIP_RESET_BIT BIT(0)

#define R_AL_DUMMY0	0x401E2068
#define SYSTEM_RUN_FLAG	BIT(16)
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

#define R_SYS_STS 0x40100000
#define VER_NUM_OFFSET 16
#define VER_NUM_MASK   (0xFF << VER_NUM_OFFSET)
const char *cros_system_chip_revision(void)
{
	static char buf[5];
	static bool init;

	if (!init) {
		uint8_t chip_ver = FIELD_GET(VER_NUM_MASK, sys_read32(R_SYS_STS));
		snprintf(buf, sizeof(buf), "%u", chip_ver);
		init = true;
	}

	return buf;
}

int cros_system_get_reset_cause(void)
{
	return reset_cause;
}

static int cros_system_rtkfp_init(void)
{
	uint32_t reg_dummy0 = sys_read32(R_AL_DUMMY0);

	reset_cause = UNKNOWN_RST;

	/* The R_AL_DUMMY0 register won't be reset by watchdog or
	 * system reset, so we can know the reset cause via this register */
	if (reg_dummy0 & SYSTEM_RUN_FLAG) {
		reset_cause = WATCHDOG_RST;
	} else {
		reset_cause = POWERUP;
		sys_set_bits(R_AL_DUMMY0, SYSTEM_RUN_FLAG);
	}

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
