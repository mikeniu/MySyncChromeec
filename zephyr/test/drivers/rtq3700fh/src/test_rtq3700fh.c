/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "drivers/imvp/rtq3700fh.h"
#include "emul/emul_rtq3700fh.h"
#include "power.h"
#include "test/drivers/test_state.h"
#include "timer.h"

#include <stdlib.h>
#include <time.h>

#include <zephyr/fff.h>
#include <zephyr/random/random.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#define RTQ3700FH_NODE DT_NODELABEL(rtq3700fh_emul)

const struct emul *emul = EMUL_DT_GET(RTQ3700FH_NODE);
const struct device *dev = DEVICE_DT_GET(RTQ3700FH_NODE);

void rtq3700fh_imvp_test_reset(void *fixture)
{
	srand(time(NULL));
	rtq3700fh_emul_reset_regs(emul);

	power_set_state(POWER_S5);

	k_msleep(10);
}

ZTEST_SUITE(rtq3700fh_imvp, drivers_predicate_pre_main, NULL,
	    rtq3700fh_imvp_test_reset, NULL, NULL);

/* =========================================================================
 * Test Cases
 * ========================================================================= */

ZTEST(rtq3700fh_imvp, test_config_mode)
{
	zassert_false(rtq3700fh_emul_is_unlocked(emul));

	power_set_state(POWER_S0);
	zassert_not_ok(
		shell_execute_cmd(get_ec_shell(), "imvp cfg_mode 0x2454"));
	zassert_false(rtq3700fh_emul_is_unlocked(emul));

	power_set_state(POWER_S5);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp cfg_mode 0x1234"));
	zassert_false(rtq3700fh_emul_is_unlocked(emul));

	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp cfg_mode 0x2454"));
	zassert_true(rtq3700fh_emul_is_unlocked(emul));

	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp cfg_mode 0x0000"));
	zassert_false(rtq3700fh_emul_is_unlocked(emul));
}

ZTEST(rtq3700fh_imvp, test_page_setting)
{
	size_t buffer_size;
	const char *buffer;

	power_set_state(POWER_S0);
	zassert_not_ok(shell_execute_cmd(get_ec_shell(), "imvp set_page 0x50"));

	power_set_state(POWER_S5);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp set_page 0x50"));

	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp dump_reg -b 0x00"));
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_not_null(strstr(buffer, "0x50"));
}

ZTEST(rtq3700fh_imvp, test_store_load_commands)
{
	power_set_state(POWER_S0);
	zassert_not_ok(shell_execute_cmd(get_ec_shell(), "imvp store_cfg"));
	zassert_not_ok(shell_execute_cmd(get_ec_shell(), "imvp load_cfg"));

	power_set_state(POWER_S5);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp store_cfg"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp load_cfg"));
}

ZTEST(rtq3700fh_imvp, test_update_happy_path)
{
	rtq3700fh_emul_set_fw_version(emul, 0x9999);

	rtq3700fh_emul_set_nvm_status(emul, 0xE0);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp update"));

	zassert_false(rtq3700fh_emul_is_unlocked(emul));
}

ZTEST(rtq3700fh_imvp, test_update_crc_trigger)
{
	rtq3700fh_emul_set_checksum(emul, 0xDEADBEEF);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp update"));
}
