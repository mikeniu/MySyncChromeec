/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mpu.h"
#include "system.h"

#include <zephyr/fff.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

FAKE_VOID_FUNC(mpu_enable);

ZTEST_USER(system_is_locked, test_syslock_console_cmd)
{
	/*
	 * Integration test that validates the syslock console command forces
	 * the system_is_locked command to return true
	 */

	const struct shell *shell_zephyr = get_ec_shell();

	shell_backend_dummy_clear_output(shell_zephyr);
	RESET_FAKE(mpu_enable);

	zassert_false(system_is_locked());

	system_disable_jump();
	zassert_equal(mpu_enable_fake.call_count, 1);

	/*
	 * TODO(b/249373175): Use CHECK_CONSOLE_CMD() macro
	 */
	zassert_ok(shell_execute_cmd(shell_zephyr, "syslock"), NULL);
	zassert_true(system_is_locked());

	RESET_FAKE(mpu_enable);
	system_disable_jump();
	zassert_equal(mpu_enable_fake.call_count, 1);
}

ZTEST_SUITE(system_is_locked, NULL, NULL, NULL, NULL, NULL);
