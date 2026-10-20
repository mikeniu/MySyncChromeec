/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "multistep_test.h"
#include "panic.h"
#include "system.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(panic_data, LOG_LEVEL_INF);

#define EXPECTED_LR_VAL 0xecec000e

/*
 * This is the index of RA in the panic_data.riscv.regs array, not the
 * architectural register number. The layout is defined in
 * https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/shim/src/panic.c;l=79;drc=f66d5f641b3a9aa8a715fb29bd0ce6b2bb96ac8a
 */
static const uint32_t RISCV_PANIC_REG_RA = 29;
static const uint32_t EXPECTED_LR = EXPECTED_LR_VAL;

static void crash_system(void)
{
	if (IS_ENABLED(CONFIG_CPU_CORTEX_M)) {
		__asm__ volatile("mov lr, %0\n"
				 "udf #0\n"
				 :
				 : "r"(EXPECTED_LR));
	} else if (IS_ENABLED(CONFIG_RISCV)) {
		__asm__ volatile("mv ra, %0\n"
				 "unimp\n"
				 :
				 : "r"(EXPECTED_LR));
	}
	zassert_unreachable();
}

static void check_panic_data(void)
{
	struct panic_data *pdata = panic_get_data();
	uint32_t lr = 0;
	uint32_t expected_flags = 0;

	if (IS_ENABLED(CONFIG_PLATFORM_EC_PANIC_HOST_EVENT)) {
		hook_notify(HOOK_CHIPSET_STARTUP);
		expected_flags |= PANIC_DATA_FLAG_OLD_HOSTEVENT;
	}

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		expected_flags |= PANIC_DATA_FLAG_RW_IMAGE;
	} else {
		expected_flags |= PANIC_DATA_FLAG_RO_IMAGE;
	}

	if (IS_ENABLED(CONFIG_ARM)) {
		lr = pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_LR];
		/*
		 * This flag is for ARM only.
		 * https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/shim/src/panic.c;l=152-154;drc=f66d5f641b3a9aa8a715fb29bd0ce6b2bb96ac8a
		 * https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/include/panic_defs.h;l=169-170;drc=ed1fa38550a14fd0aa35f1d7c79bf0aa3069acf6
		 */
		expected_flags |= PANIC_DATA_FLAG_FRAME_VALID;
	} else if (IS_ENABLED(CONFIG_RISCV)) {
		lr = pdata->riscv.regs[RISCV_PANIC_REG_RA];
	} else {
		zassert_true(false, "Unsupported architecture");
	}

	/* Make sure Link Register is stored correctly. */
	zassert_equal(lr, EXPECTED_LR);

	/* Check panic flags. */
	zassert_equal(pdata->flags, expected_flags);
}

static void test_crash(void)
{
	LOG_INF("Crash the system!");
	cflush();
	crash_system();
	zassert_unreachable();
}

static void test_soft_reboot(void)
{
	check_panic_data();
	LOG_INF("Perform soft reboot");
	cflush();
	system_reset(0);
	zassert_unreachable();
}

static void test_hard_reboot(void)
{
	check_panic_data();
	LOG_INF("Perform hard reboot");
	cflush();
	system_reset(SYSTEM_RESET_HARD);
	zassert_unreachable();
}

static void test_check_panic(void)
{
	LOG_INF("Check panic data");
	check_panic_data();
}

static void (*test_steps[])(void) = { test_crash, test_soft_reboot,
				      test_hard_reboot, test_check_panic };

MULTISTEP_TEST(panic_data, test_steps)
