/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "uart.h"

#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST(host_cmd_host_commands, test_get_command_versions__v1)
{
	struct ec_response_get_cmd_versions response;
	struct ec_params_get_cmd_versions_v1 params = {
		.cmd = EC_CMD_GET_CMD_VERSIONS
	};
	int rv;

	rv = ec_cmd_get_cmd_versions_v1(NULL, &params, &response);

	zassert_ok(rv, "Got %d", rv);
	zassert_equal(EC_VER_MASK(0) | EC_VER_MASK(1), response.version_mask);
}

ZTEST(host_cmd_host_commands, test_get_command_versions__invalid_cmd)
{
	struct ec_response_get_cmd_versions response;
	struct ec_params_get_cmd_versions_v1 params = {
		/* Host command doesn't exist */
		.cmd = UINT16_MAX,
	};
	int rv;

	rv = ec_cmd_get_cmd_versions_v1(NULL, &params, &response);

	zassert_equal(EC_RES_INVALID_PARAM, rv, "Got %d", rv);
}

ZTEST(host_cmd_host_commands, test_get_comms_status)
{
	struct ec_response_get_comms_status response;
	int rv;

	rv = ec_cmd_get_comms_status(NULL, &response);

	zassert_ok(rv, "Got %d", rv);

	/* Unit test host commands are processed synchronously, so always expect
	 * the EC to be not busy processing another.
	 */
	zassert_false(response.flags);
}

#ifndef CONFIG_EC_HOST_CMD
ZTEST(host_cmd_host_commands, test_resend_response)
{
	struct host_cmd_handler_args args =
		(struct host_cmd_handler_args)BUILD_HOST_COMMAND_SIMPLE(
			EC_CMD_RESEND_RESPONSE, 0);
	int rv;

	rv = host_command_process(&args);
	zassert_ok(rv);

	/* The way we trigger host commands in tests doesn't cause results to
	 * get saved (it happens outside of host_command_process), so we cannot
	 * verify the resent response itself.
	 *
	 * TODO: test at least one host command through the ESPI interface.
	 */
}

#else
ZTEST(host_cmd_host_commands, test_resend_response)
{
	struct host_cmd_handler_args args =
		(struct host_cmd_handler_args)BUILD_HOST_COMMAND_SIMPLE(
			EC_CMD_RESEND_RESPONSE, 0);
	int rv;

	/* Send invalid erase parameters not to corrupt flash */
	struct ec_params_flash_erase erase_params = {
		.offset = 0x10000,
		.size = 0,
	};

	struct host_cmd_handler_args erase_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_FLASH_ERASE, 0, erase_params);

	rv = host_command_process(&erase_args);

	zassert_equal(EC_RES_IN_PROGRESS, rv);

	/* Give time for handler that returns IN_PROGRESS to execute. It runs
	 * in sysworkq
	 */
	k_sleep(K_MSEC(100));
	/* Expect error because of incorrect parameters - size = 0 */
	rv = host_command_process(&args);
	zassert_equal(EC_RES_ERROR, rv);

	rv = host_command_process(&args);
	zassert_equal(EC_RES_UNAVAILABLE, rv);
}
#endif

ZTEST(host_cmd_host_commands, test_get_proto_version)
{
	struct ec_response_proto_version response;
	int rv;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_PROTO_VERSION, 0, response);

	rv = host_command_process(&args);

	zassert_ok(rv, "Got %d", rv);
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(EC_PROTO_VERSION, response.version);
}

ZTEST(host_cmd_host_commands, test_hello)
{
	struct ec_response_hello response;
	struct ec_params_hello params;
	int rv;
	uint32_t params_to_test[] = { 0x0, 0xaaaaaaaa, 0xffffffff };
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HELLO, 0, response, params);

	for (int i = 0; i < ARRAY_SIZE(params_to_test); i++) {
		params.in_data = params_to_test[i];

		rv = host_command_process(&args);

		zassert_ok(rv, "Got %d, in_data: %x", rv, params_to_test[i]);
		zassert_equal(args.response_size, sizeof(response));
		zassert_equal(params_to_test[i] + 0x01020304, response.out_data,
			      "in_data: %x", params_to_test[i]);
	}
}

ZTEST(host_cmd_host_commands, test_ap_fw_state)
{
	const struct shell *shell_zephyr = get_ec_shell();
	struct ec_params_ap_fw_state params;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_AP_FW_STATE, 0, params);
	const char *outbuffer;
	size_t buffer_size;
	int rv;

	/* Flush the console buffer before we start. */
	shell_backend_dummy_clear_output(shell_zephyr);

	/* Test 0: Verify console command shows 0 states initially */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "0 states:") != NULL,
		     "Initial state count not zero: %s", outbuffer);

	/* Test 1: Save first state */
	params.state = 0x12345678;
	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(strstr(outbuffer, "AP_FW 12345678") != NULL,
		     "Invalid console output %s", outbuffer);

	/* Test 2: Verify console command shows it */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, ": 12345678") != NULL,
		     "Console command output missing state: %s", outbuffer);

	/* Test 3: Save second state */
	params.state = 0x87654321;
	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);

	/* Test 4: Verify console command shows both */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, ": 12345678") != NULL,
		     "Console command output missing first state: %s",
		     outbuffer);
	zassert_true(strstr(outbuffer, ": 87654321") != NULL,
		     "Console command output missing second state: %s",
		     outbuffer);

	/* Test 5: Duplicate suppression */
	params.state = 0x87654321;
	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);

	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	/* The count should still be 2. */
	zassert_true(strstr(outbuffer, "2 states:") != NULL,
		     "Duplicate was saved! Output: %s", outbuffer);

	/* Test 6: Buffer wraparound */
	for (int i = 3; i <= 12; i++) {
		params.state = i;
		rv = host_command_process(&args);
		zassert_ok(rv, "Got %d for state %d", rv, i);
	}

	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	/* The count should be 10. */
	zassert_true(strstr(outbuffer, "10 states:") != NULL,
		     "Buffer count not 10! Output: %s", outbuffer);
	/* State 0x12345678 should be gone. */
	zassert_true(strstr(outbuffer, "12345678") == NULL,
		     "Old state 12345678 not overwritten! Output: %s",
		     outbuffer);
	/* State 0x87654321 should be gone. */
	zassert_true(strstr(outbuffer, "87654321") == NULL,
		     "Old state 87654321 not overwritten! Output: %s",
		     outbuffer);
	/* State 12 (c in hex) should be there. */
	zassert_true(strstr(outbuffer, ": c") != NULL,
		     "New state 'c' not in buffer! Output: %s", outbuffer);

	/* Test 7: Clear buffer */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens clear"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "Buffer cleared") != NULL,
		     "Buffer clear failed! Output: %s", outbuffer);

	/* Verify it's empty now */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "0 states:") != NULL,
		     "Buffer not empty after clear! Output: %s", outbuffer);

	/* Test 8: Monotonically increasing timestamps */
	/* Add first state */
	params.state = 0x11111111;
	rv = host_command_process(&args);
	zassert_ok(rv);

	/* Add second state */
	params.state = 0x22222222;
	rv = host_command_process(&args);
	zassert_ok(rv);

	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	/* Parse output to get timestamps */
	const char *p1 = strstr(outbuffer, "11111111");
	const char *p2 = strstr(outbuffer, "22222222");
	zassert_true(p1 != NULL && p2 != NULL, "States not found in output");

	zassert_true(p1 < p2, "States in wrong order in output: p1=%p, p2=%p",
		     p1, p2);
}

ZTEST_SUITE(host_cmd_host_commands, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);
