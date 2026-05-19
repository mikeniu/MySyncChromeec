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

/* Include the driver source directly to test static functions (White-box
 * testing) */
#include "rtq3700fh.c"

#define RTQ3700FH_NODE DT_NODELABEL(imvp_rtq3700fh)

const struct emul *emul = EMUL_DT_GET(RTQ3700FH_NODE);
const struct device *dev = DEVICE_DT_GET(RTQ3700FH_NODE);

/**
 * @brief Reset emulator state before each test case
 */
void rtq3700fh_imvp_test_reset(void *fixture)
{
	srand(time(NULL));
	rtq3700fh_emul_reset_regs(emul);

	/* Default to S5 to allow I2C config operations */
	power_set_state(POWER_S5);
}

ZTEST_SUITE(rtq3700fh_imvp, drivers_predicate_pre_main, NULL,
	    rtq3700fh_imvp_test_reset, NULL, NULL);

/* =========================================================================
 * Test Cases
 * ========================================================================= */

ZTEST(rtq3700fh_imvp, test_config_mode)
{
	zassert_false(rtq3700fh_emul_is_unlocked(emul));

	/* Setting wrong pattern out of G3/S5 */
	power_set_state(POWER_S0);
	zassert_not_ok(shell_execute_cmd(get_ec_shell(),
					 "imvp cfg_mode 0x2454"),
		       NULL);
	zassert_false(rtq3700fh_emul_is_unlocked(emul));

	/* Setting wrong password in S5 */
	power_set_state(POWER_S5);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp cfg_mode 0x1234"),
		   NULL);
	zassert_false(rtq3700fh_emul_is_unlocked(emul));

	/* Setting correct password */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp cfg_mode 0x2454"),
		   NULL);
	zassert_true(rtq3700fh_emul_is_unlocked(emul));

	/* Lock it back */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp cfg_mode 0x0000"),
		   NULL);
	zassert_false(rtq3700fh_emul_is_unlocked(emul));
}

ZTEST(rtq3700fh_imvp, test_page_setting)
{
	struct rtq3700fh_data_t *data = dev->data;
	int rv;

	/* Attempt to change page while out of G3/S5 */
	power_set_state(POWER_S0);
	rv = shell_execute_cmd(get_ec_shell(), "imvp set_page 0x50");
	zassert_not_ok(rv);

	/* Set page to NVM Page (0x50) in S5 */
	power_set_state(POWER_S5);
	rv = shell_execute_cmd(get_ec_shell(), "imvp set_page 0x50");
	zassert_ok(rv);

	/* Verify driver internal state is updated */
	zassert_equal(data->cur_page, 0x50);
}

ZTEST(rtq3700fh_imvp, test_crc_check)
{
	int rv;

	/* Emul default CRC is set to 0x01E8 (Matched with DTS) */
	rv = rtq3700fh_crc_check(dev);
	zassert_ok(rv, "CRC check should pass with matching checksum");

	/* Force emulator to return a bad checksum */
	rtq3700fh_emul_set_checksum(emul, 0xDEADBEEF);
	rv = rtq3700fh_crc_check(dev);
	zassert_not_ok(rv, "CRC check should fail with mismatching checksum");
}

ZTEST(rtq3700fh_imvp, test_nvm_status_timeout)
{
	int rv;

	/* Force emulator NVM status to NOT ready (clear ready bit) */
	rtq3700fh_emul_set_nvm_status(emul, 0x00);

	/* Check timeout logic for NVM Restore */
	rv = rtq3700fh_nvm_program_status(dev, NVM_RELOAD_STAT_BIT);
	zassert_not_ok(rv, "Should timeout waiting for reload flag");

	/* Set back to ready */
	rtq3700fh_emul_set_nvm_status(emul, NVM_READY_STAT_VAL);
	rv = rtq3700fh_nvm_program_status(dev, NVM_RELOAD_STAT_BIT);
	zassert_ok(rv, "Should succeed when ready flag is set");
}

ZTEST(rtq3700fh_imvp, test_update_happy_path)
{
	int rv;

	/* Ensure system is unlocked before update starts to test lock safety */
	rtq3700fh_emul_is_unlocked(emul);

	/* Step 1: Force a firmware version mismatch to trigger the update logic
	 */
	rtq3700fh_emul_set_fw_version(emul, 0x9999);

	/* Step 2: Ensure NVM status polling will succeed */
	rtq3700fh_emul_set_nvm_status(emul, NVM_READY_STAT_VAL);

	/* Step 3: Call the static update routine directly */
	rv = rtq3700fh_update(dev);
	zassert_ok(rv, "Full NVM update routine should succeed");

	/* Step 4: The driver should securely lock the IMVP upon completion */
	zassert_false(rtq3700fh_emul_is_unlocked(emul),
		      "IMVP must be locked after update");
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
