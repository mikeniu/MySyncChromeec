/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "system.h"
#include "tinysys_reg.h"

#include <zephyr/cache.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define WDT_EN BIT(31)
#define DISABLE_WDT 0x000FFFFF
#define KICK_WDT 0x1
#define WDT_INSTANT_TRIGGER 0x0

const char *system_get_chip_vendor(void)
{
	return "Mediatek";
}

const char *system_get_chip_name(void)
{
	return "MT81xx SCP";
}

const char *system_get_chip_revision(void)
{
	return "";
}

void system_reset(int flags)
{
	ARG_UNUSED(flags);

	irq_lock();

	/* Set reboot flag to let AP know we are ready to reboot */
	sys_write32(CORE_RDY_TO_REBOOT, SCP_GPR_HART0_REBOOT);
	sys_write32(CORE_RDY_TO_REBOOT, SCP_GPR_HART1_REBOOT);

	sys_cache_data_flush_all();

	/* Trigger WDT ISR to notify AP */
	sys_write32(DISABLE_WDT, CORE0_WDT_CFG);
	sys_write32(WDT_EN | WDT_INSTANT_TRIGGER, CORE0_WDT_CFG);
	sys_write32(KICK_WDT, CORE0_WDT_KICK);

	while (1) {
		sys_cache_data_flush_all();
		__asm__ volatile("wfi");
	}
	__builtin_unreachable();
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
}

uint32_t crec_flash_get_protect(void)
{
	return 0;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	return EC_ERROR_UNIMPLEMENTED;
}
