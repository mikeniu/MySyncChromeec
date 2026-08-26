/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST_USER(host_cmd_console_hc, test_hc_invalid_args)
{
	const struct shell *ec_sh = get_ec_shell();
	int rv;

	/* No args */
	rv = shell_execute_cmd(ec_sh, "hc");
	zassert_not_equal(rv, 0);

	/* Invalid chunk format */
	rv = shell_execute_cmd(ec_sh, "hc 1/a 01020304");
	zassert_not_equal(rv, 0);

	/* Chunk num > total chunks */
	rv = shell_execute_cmd(ec_sh, "hc 2/1 01020304");
	zassert_not_equal(rv, 0);

	/* Zero chunk or zero total */
	rv = shell_execute_cmd(ec_sh, "hc 0/1 01020304");
	zassert_not_equal(rv, 0);

	/* Invalid hex characters */
	rv = shell_execute_cmd(ec_sh, "hc 0102030g");
	zassert_not_equal(rv, 0);

	/* Odd-length hex characters */
	rv = shell_execute_cmd(ec_sh, "hc 0102030");
	zassert_not_equal(rv, 0);
}

ZTEST_USER(host_cmd_console_hc, test_hc_chunk_out_of_order)
{
	const struct shell *ec_sh = get_ec_shell();
	int rv;

	/* Send chunk 1/3 */
	rv = shell_execute_cmd(ec_sh, "hc 1/3 03f20b00");
	zassert_ok(rv);

	/* Send chunk 3/3 out of order (skipping 2/3) */
	rv = shell_execute_cmd(ec_sh, "hc 3/3 00000000");
	zassert_not_equal(rv, 0);
}

ZTEST_USER(host_cmd_console_hc, test_hc_single_chunk_get_protocol_info)
{
	const struct shell *ec_sh = get_ec_shell();
	const char *output;
	size_t output_size;
	int rv;

	/*
	 * EC_CMD_GET_PROTOCOL_INFO (0x000b):
	 * struct ec_host_request:
	 *   struct_version: 0x03
	 *   checksum: 0xf2 (0x03 + 0xf2 + 0x0b = 0x100)
	 *   command: 0x000b (0x0b, 0x00)
	 *   command_version: 0x00
	 *   reserved: 0x00
	 *   data_len: 0x0000 (0x00, 0x00)
	 */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 03f20b0000000000");
	zassert_ok(rv, "shell_execute_cmd failed: %d", rv);

	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output, "No shell output received");
	zassert_true(output_size > 0, "Empty shell output");

	/* Expected output starts with HC(1/1): */
	const char *hc_resp = strstr(output, "HC(1/1):");
	zassert_not_null(hc_resp, "Missing HC(1/1): prefix in %s", output);

	/* Check for struct_version 03 in response hex */
	zassert_not_null(strstr(hc_resp, "03"), "Missing version 3 in %s",
			 hc_resp);
}

ZTEST_USER(host_cmd_console_hc, test_hc_multi_chunk_request)
{
	const struct shell *ec_sh = get_ec_shell();
	const char *output;
	size_t output_size;
	int rv;

	/* First chunk should be silent (no output) */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 1/2 03f20b00");
	zassert_ok(rv);

	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_equal(output_size, 0, "Intermediate chunk produced output: %s",
		      output ? output : "");

	/* Second chunk completes the packet and produces response */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 2/2 00000000");
	zassert_ok(rv);

	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_true(output_size > 0);
	zassert_not_null(strstr(output, "HC(1/1):"));
}

ZTEST_USER(host_cmd_console_hc, test_hc_multi_chunk_response)
{
	const struct shell *ec_sh = get_ec_shell();
	const char *output;
	size_t output_size;
	int rv;

	/*
	 * EC_CMD_GET_VERSION (0x0002):
	 * Returns struct ec_response_get_version (> 100 bytes).
	 * Request packet: 03 fd 02 00 00 00 00 00 (0x03 + 0xfd + 0x02 = 0x102
	 * => csum 0xfb? wait: 0x03 + 0xfb + 0x02 = 0x100 => csum is 0xfb).
	 */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 03fb020000000000");
	zassert_ok(rv);

	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_true(output_size > 0);

	/* Should produce HC(1/2): and HC(2/2): */
	zassert_not_null(strstr(output, "HC(1/2):"), "Missing HC(1/2) in %s",
			 output);
	zassert_not_null(strstr(output, "HC(2/2):"), "Missing HC(2/2) in %s",
			 output);
}

ZTEST_USER(host_cmd_console_hc, test_hc_checksum_error_response)
{
	const struct shell *ec_sh = get_ec_shell();
	const char *output;
	size_t output_size;
	int rv;

	/* Bad checksum: byte 1 is 0x00 instead of 0xf2 */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 03000b0000000000");
	zassert_ok(rv);

	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	/* Should emit HC(1/1): with EC_RES_INVALID_CHECKSUM */
	const char *hc_resp = strstr(output, "HC(1/1):");
	zassert_not_null(hc_resp, "Expected error response packet in %s",
			 output);
}

ZTEST_SUITE(host_cmd_console_hc, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
