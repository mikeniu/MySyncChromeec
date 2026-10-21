/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mpu.h"
#if defined(CONFIG_TEST_CORTEX_M_MPU)
#include "ram_lock.h"
#endif
#include "system.h"

#include <zephyr/fff.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

#if defined(CONFIG_TEST_CORTEX_M_MPU)
static const struct arm_mpu_region test_mpu_regions[] = {
	{ .base = CONFIG_FLASH_BASE_ADDRESS },
	{ .base = CONFIG_SRAM_BASE_ADDRESS },
	{ .base = 0 },
};

const struct arm_mpu_config mpu_config = {
	.num_regions = ARRAY_SIZE(test_mpu_regions),
	.mpu_regions = test_mpu_regions,
};

FAKE_VALUE_FUNC(int, ram_lock_config_lock_region, uint8_t, uint32_t, uint32_t);
#else
FAKE_VOID_FUNC(mpu_enable);
#endif

ZTEST_USER(system_is_locked, test_syslock_console_cmd)
{
	/*
	 * Integration test that validates the syslock console command forces
	 * the system_is_locked command to return true
	 */

	const struct shell *shell_zephyr = get_ec_shell();

	shell_backend_dummy_clear_output(shell_zephyr);
#if defined(CONFIG_TEST_CORTEX_M_MPU)
	RESET_FAKE(ram_lock_config_lock_region);
#else
	RESET_FAKE(mpu_enable);
#endif

	zassert_false(system_is_locked());

#if defined(CONFIG_TEST_CORTEX_M_MPU)
	test_mpu_regs.RASR = 0;
	system_disable_jump();
	zassert_equal(ram_lock_config_lock_region_fake.call_count, 0);
	zassert_true(test_mpu_regs.RASR & BIT(0));

	arch_pre_system_jump();
	zassert_false(test_mpu_regs.RASR & BIT(0));
#else
	system_disable_jump();
	zassert_equal(mpu_enable_fake.call_count, 1);
#endif

	/*
	 * TODO(b/249373175): Use CHECK_CONSOLE_CMD() macro
	 */
	zassert_ok(shell_execute_cmd(shell_zephyr, "syslock"), NULL);
	zassert_true(system_is_locked());

#if defined(CONFIG_TEST_CORTEX_M_MPU)
	test_mpu_regs.RASR = 0;
	system_disable_jump();
	zassert_equal(ram_lock_config_lock_region_fake.call_count, 2);
	zassert_true(test_mpu_regs.RASR & BIT(0));
#else
	RESET_FAKE(mpu_enable);
	system_disable_jump();
	zassert_equal(mpu_enable_fake.call_count, 1);
#endif
}

ZTEST_SUITE(system_is_locked, NULL, NULL, NULL, NULL, NULL);
