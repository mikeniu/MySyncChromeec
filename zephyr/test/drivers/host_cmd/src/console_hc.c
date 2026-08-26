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

#define HC_ERR_INVALID_PARAM "HC:03fa030000000000"
#define HC_ERR_REQUEST_TRUNCATED "HC:03f00d0000000000"
#define HC_ERR_CHECKSUM "HC:03f6070000000000"
#define HC_RESP_IN_PROGRESS "HC:03f5080000000000"
#define HC_REQ_PROTOCOL_INFO "03f20b0000000000"
#define HC_REQ_BAD_CHECKSUM "03000b0000000000"
#define HC_REQ_IN_PROGRESS "03bfff3f00000000"
#define HC_REQ_GET_VERSION "03fb020000000000"
#define HC_RESP_SINGLE_PREFIX "HC:03"

ZTEST_USER(host_cmd_console_hc, test_hc_invalid_args)
{
	const struct shell *ec_sh = get_ec_shell();
	const char *output;
	size_t output_size;
	int rv;

	/* No args: returns error packet with EC_RES_INVALID_PARAM (3) */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc");
	zassert_not_equal(rv, 0);
	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_not_null(strstr(output, HC_ERR_INVALID_PARAM),
			 "Expected INVALID_PARAM error packet in %s", output);

	/* Invalid chunk format */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 1/g 01020304");
	zassert_not_equal(rv, 0);
	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_not_null(strstr(output, HC_ERR_INVALID_PARAM));

	/* Chunk num > total chunks */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 2/1 01020304");
	zassert_not_equal(rv, 0);
	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_not_null(strstr(output, HC_ERR_INVALID_PARAM));

	/* Invalid encoding characters */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 0102030g");
	zassert_not_equal(rv, 0);
	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_not_null(strstr(output, HC_ERR_INVALID_PARAM));

	/* Odd-length hex characters */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 0102030");
	zassert_not_equal(rv, 0);
	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_not_null(strstr(output, HC_ERR_INVALID_PARAM));
}

ZTEST_USER(host_cmd_console_hc, test_hc_chunk_out_of_order)
{
	const struct shell *ec_sh = get_ec_shell();
	const char *output;
	size_t output_size;
	int rv;

	/* Send chunk 1/3 */
	rv = shell_execute_cmd(ec_sh, "hc 1/3 03f20b00");
	zassert_ok(rv);

	/* Send chunk 3/3 out of order (skipping 2/3): emits REQUEST_TRUNCATED
	 */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 3/3 00000000");
	zassert_not_equal(rv, 0);
	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_not_null(strstr(output, HC_ERR_REQUEST_TRUNCATED),
			 "Expected REQUEST_TRUNCATED error packet in %s",
			 output);
}

ZTEST_USER(host_cmd_console_hc, test_hc_single_chunk_get_protocol_info)
{
	const struct shell *ec_sh = get_ec_shell();
	const char *output;
	size_t output_size;
	char cmd_buf[64];
	int rv;

	snprintf(cmd_buf, sizeof(cmd_buf), "hc %s", HC_REQ_PROTOCOL_INFO);
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, cmd_buf);
	zassert_ok(rv, "shell_execute_cmd failed: %d", rv);

	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output, "No shell output received");
	zassert_true(output_size > 0, "Empty shell output");

	const char *hc_resp = strstr(output, HC_RESP_SINGLE_PREFIX);
	zassert_not_null(hc_resp, "Missing %s prefix in %s",
			 HC_RESP_SINGLE_PREFIX, output);
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
	if (output_size > 0 && output) {
		zassert_is_null(strstr(output, "HC:"),
				"Intermediate chunk produced HC output: %s",
				output);
	}

	/* Second chunk completes the packet and produces response */
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, "hc 2/2 00000000");
	zassert_ok(rv);

	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_true(output_size > 0);
	zassert_not_null(strstr(output, HC_RESP_SINGLE_PREFIX));
}

ZTEST_USER(host_cmd_console_hc, test_hc_multi_chunk_response)
{
	const struct shell *ec_sh = get_ec_shell();
	const char *output;
	size_t output_size;
	char cmd_buf[64];
	int rv;

	snprintf(cmd_buf, sizeof(cmd_buf), "hc %s", HC_REQ_GET_VERSION);
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, cmd_buf);
	zassert_ok(rv);

	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_true(output_size > 0);

	/* With 32-byte chunks, a ~104 byte response spans 4 chunks */
	zassert_not_null(strstr(output, "HC:1/4:"), "Missing HC:1/4: in %s",
			 output);
	zassert_not_null(strstr(output, "HC:2/4:"), "Missing HC:2/4: in %s",
			 output);
	zassert_not_null(strstr(output, "HC:3/4:"), "Missing HC:3/4: in %s",
			 output);
	zassert_not_null(strstr(output, "HC:4/4:"), "Missing HC:4/4: in %s",
			 output);
}

ZTEST_USER(host_cmd_console_hc, test_hc_checksum_error_response)
{
	const struct shell *ec_sh = get_ec_shell();
	const char *output;
	size_t output_size;
	char cmd_buf[64];
	int rv;

	snprintf(cmd_buf, sizeof(cmd_buf), "hc %s", HC_REQ_BAD_CHECKSUM);
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, cmd_buf);
	zassert_ok(rv);

	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	const char *hc_resp = strstr(output, HC_ERR_CHECKSUM);
	zassert_not_null(hc_resp, "Expected error response packet in %s",
			 output);
}

#define TEST_CMD_IN_PROGRESS 0x3fff

static enum ec_status
test_hc_in_progress_handler(struct host_cmd_handler_args *args)
{
	return EC_RES_IN_PROGRESS;
}
DECLARE_HOST_COMMAND(TEST_CMD_IN_PROGRESS, test_hc_in_progress_handler,
		     EC_VER_MASK(0));

ZTEST_USER(host_cmd_console_hc, test_hc_in_progress_flow)
{
	const struct shell *ec_sh = get_ec_shell();
	const char *output;
	size_t output_size;
	char cmd_buf[64];
	int rv;

	snprintf(cmd_buf, sizeof(cmd_buf), "hc %s", HC_REQ_IN_PROGRESS);
	shell_backend_dummy_clear_output(ec_sh);
	rv = shell_execute_cmd(ec_sh, cmd_buf);
	zassert_ok(rv);

	output = shell_backend_dummy_get_output(ec_sh, &output_size);
	zassert_not_null(output);
	zassert_true(output_size > 0);

	const char *hc_resp = strstr(output, HC_RESP_IN_PROGRESS);
	zassert_not_null(hc_resp, "Expected IN_PROGRESS packet in %s", output);
}

ZTEST_SUITE(host_cmd_console_hc, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
