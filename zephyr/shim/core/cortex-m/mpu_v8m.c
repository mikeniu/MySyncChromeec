/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
#include "mpu.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/arch/cpu.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>

#include <cmsis_core.h>
#include <cortex_m/cmse.h>

LOG_MODULE_REGISTER(shim_mpu, LOG_LEVEL_ERR);

void mpu_enable(void)
{
	/* MPU is configured and enabled by Zephyr init code. */
}

#ifdef CONFIG_PLATFORM_EC_ROLLBACK_MPU_PROTECT
static int mpu_static_rollback_region_id = -1;

test_mockable int mpu_lock_rollback(bool lock)
{
	if (mpu_static_rollback_region_id < 0)
		return -EINVAL;

	/*
	 * On ARMv8-M, MPU_RBAR.AP[2:1] does not have a "Privileged No-Access"
	 * encoding (DDI 0553 Sec D1.2.171). Disabling the MPU region here
	 * relies on the privileged default memory map being disabled
	 * (MPU_CTRL.PRIVDEFENA == 0 via CONFIG_MPU_DISABLE_BACKGROUND_MAP=y,
	 * DDI 0553 Sec D1.2.168) to generate a MemManage fault on access.
	 */
	MPU->RNR = mpu_static_rollback_region_id;
	if (lock)
		MPU->RLAR &= ~MPU_RLAR_EN_Msk;
	else
		MPU->RLAR |= MPU_RLAR_EN_Msk;

	/* MPU register updates must be visible before later memory accesses. */
	barrier_dsync_fence_full();
	barrier_isync_fence_full();

	return 0;
}

/*
 * When PRE_KERNEL_1 callbacks are executed (see z_cstart() at kernel/init.c)
 * the static MPU regions are already installed by z_arm_mpu_init().
 */
static int mpu_add_static_rollback_regions(void)
{
	/*
	 * On ARMv8-M, overlapping enabled MPU regions fault (DDI 0553 Sec
	 * B10.1, rule RLLLP), so the rollback region is defined as a separate
	 * static entry in mpu_regions[] (e.g., shim/chip/rtkfp/mpu_regions.c)
	 * and enabled during z_arm_mpu_init(). Look up its region index while
	 * it is enabled (required for TT_RESP.MRVALID, DDI 0553 Sec D1.2.269)
	 * and then immediately lock (disable) it.
	 */
	int region = arm_cmse_mpu_region_get(
		CONFIG_MAPPED_STORAGE_BASE +
		DT_REG_ADDR(DT_NODELABEL(rollback0)));

	if (region < 0)
		return -EINVAL;

	mpu_static_rollback_region_id = region;
	LOG_DBG("Rollback MPU region set at %d", region);

	return mpu_lock_rollback(true);
}
SYS_INIT(mpu_add_static_rollback_regions, PRE_KERNEL_1, 50);
#endif /* CONFIG_PLATFORM_EC_ROLLBACK_MPU_PROTECT */
