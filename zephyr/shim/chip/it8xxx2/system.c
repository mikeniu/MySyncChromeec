/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "system.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util_macro.h>

#include <chip_chipregs.h>
#include <cpu.h>
#include <ilm.h>

LOG_MODULE_REGISTER(shim_it8xxx2_system, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ilm), okay)
/*
 * Bit 3 of the SCAR High byte (SCARnH) disables the ILM mapping when set to 1.
 * It puts the RAM block into DLM (Data Local Memory) mode, bypassing ILM flash
 * interception.
 */
#define IT8XXX2_SMFI_SCARH_DISABLE BIT(3)

/*
 * The SCAR registers are 3 bytes wide (Low, Middle, High).
 * DT_REG_ADDR_BY_IDX returns the base address (the Low byte).
 */
#define DISABLE_ILM_MAPPING(idx, node_id)                          \
	do {                                                       \
		uintptr_t addr = DT_REG_ADDR_BY_IDX(node_id, idx); \
		sys_write8(IT8XXX2_SMFI_SCARH_DISABLE, addr + 2);  \
	} while (0)

void chip_pre_system_jump(void)
{
	/* Disable all ILM mappings before sysjump, otherwise the new image
	 * may attempt to execute stale code from the previous image.
	 */
	LISTIFY(DT_NUM_REGS(DT_NODELABEL(ilm)), DISABLE_ILM_MAPPING, (;),
		DT_NODELABEL(ilm));
}
#endif

uintptr_t system_get_fw_reset_vector(uintptr_t base)
{
	/*
	 * Because our reset vector is at the beginning of image copy
	 * (see init.S). So I just need to return 'base' here and EC will jump
	 * to the reset vector.
	 */
	return base;
}

static void chip_bbram_status_check(void)
{
	const struct device *bbram_dev;
	int res;

	bbram_dev = DEVICE_DT_GET(DT_NODELABEL(bbram));
	if (!device_is_ready(bbram_dev)) {
		LOG_ERR_DEVICE_NOT_READY(bbram_dev);
		return;
	}

	res = bbram_check_invalid(bbram_dev);
	if (res != 0 && res != -ENOTSUP) {
		LOG_INF("VBAT power drop!");
	}
}

static int chip_system_init(void)
{
	/* Check BBRAM power status. */
	chip_bbram_status_check();

	return 0;
}

/*
 * The priority should be lower than CONFIG_BBRAM_INIT_PRIORITY.
 */
#if (CONFIG_CROS_SYSTEM_IT8XXX2_PRE_INIT_PRIORITY <= CONFIG_BBRAM_INIT_PRIORITY)
#error CONFIG_CROS_SYSTEM_IT8XXX2_PRE_INIT_PRIORITY must greater than \
	CONFIG_BBRAM_INIT_PRIORITY
#endif
SYS_INIT(chip_system_init, PRE_KERNEL_1,
	 CONFIG_CROS_SYSTEM_IT8XXX2_PRE_INIT_PRIORITY);
