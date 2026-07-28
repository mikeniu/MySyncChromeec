/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "test_state.h"

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST(console_cmd_pwrseq, test_cmd_apreset)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "apreset"),
		   "apreset console command failed");
}

ZTEST(console_cmd_pwrseq, test_cmd_apshutdown)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "apshutdown"),
		   "apshutdown console command failed");
}

ZTEST(console_cmd_pwrseq, test_cmd_powerinfo)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should be in G3");

	shell_backend_dummy_clear_output(shell_zephyr);
	zassert_ok(shell_execute_cmd(shell_zephyr, "powerinfo"),
		   "powerinfo console command failed");
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_not_null(strstr(outbuffer, "= G3,"),
			 "powerinfo did not report G3: %s", outbuffer);
}

ZTEST_SUITE(console_cmd_pwrseq, ap_power_predicate_post_main, NULL, NULL, NULL,
	    NULL);
