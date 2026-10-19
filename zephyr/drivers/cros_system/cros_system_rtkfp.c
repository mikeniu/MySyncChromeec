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

#ifndef CONFIG_SOC_RTS5817
/*
 * When built for a host unit test (native_sim) there is no RTS5817
 * register block, so back the registers with RAM.
 */
uint32_t cros_system_rtkfp_test_r_chip_reset;
uint32_t cros_system_rtkfp_test_r_sys_sts;

#define R_CHIP_RESET ((uintptr_t)&cros_system_rtkfp_test_r_chip_reset)
#define R_SYS_STS ((uintptr_t)&cros_system_rtkfp_test_r_sys_sts)
#else
#define R_CHIP_RESET 0x401E2100
#define R_SYS_STS 0x40100000
#endif

#define CHIP_RESET_BIT BIT(0)
#define VER_NUM_MASK GENMASK(23, 16)

/* Longest "%u" of a uint8_t is 3 digits, plus the null terminator */
#define MAX_REV_STR_LEN 4

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
	static char buf[MAX_REV_STR_LEN];
	static bool init;

	if (!init) {
		snprintf(buf, sizeof(buf), "%u",
			 cros_system_rtkfp_get_chip_version(
				 sys_read32(R_SYS_STS)));
		init = true;
	}

	return buf;
}

int cros_system_get_reset_cause(void)
{
	return UNKNOWN_RST;
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
