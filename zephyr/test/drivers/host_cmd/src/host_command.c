/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "system.h"
#include "test/drivers/test_mocks.h"
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

#ifndef CONFIG_EC_HOST_CMD
extern struct host_cmd_handler_args *pending_args;
extern struct host_packet *pkt0;
void host_packet_respond(struct host_cmd_handler_args *args);

#define PKT_BUFFER_SIZE 128

static struct host_packet test_pkt;
static uint8_t test_resp_buf[PKT_BUFFER_SIZE] __aligned(4);
static uint8_t test_req_buf[PKT_BUFFER_SIZE + 4] __aligned(4);
static struct ec_host_request *test_req =
	(struct ec_host_request *)test_req_buf;
static struct ec_params_hello *test_hello_p =
	(struct ec_params_hello *)(test_req_buf +
				   sizeof(struct ec_host_request));
static struct ec_host_response *test_resp =
	(struct ec_host_response *)test_resp_buf;
static struct ec_response_hello *test_hello_r =
	(struct ec_response_hello *)(test_resp_buf +
				     sizeof(struct ec_host_response));
static struct ec_response_get_chip_info *test_chip_info_r =
	(struct ec_response_get_chip_info *)(test_resp_buf +
					     sizeof(struct ec_host_response));

static int test_response_count;
static int test_stub_response_count;

static void test_hostcmd_respond(struct host_packet *pkt)
{
	test_response_count++;
}

static void test_stub_send_response(struct host_cmd_handler_args *args)
{
	test_stub_response_count++;
}

static uint8_t calc_pkt_checksum(const void *buf, int size)
{
	const uint8_t *b = (const uint8_t *)buf;
	int c = 0;

	for (int i = 0; i < size; ++i) {
		c += b[i];
	}

	return (uint8_t)(-c);
}

static void test_hostcmd_fill_default(void)
{
	memset(test_req_buf, 0, sizeof(test_req_buf));
	memset(test_resp_buf, 0, sizeof(test_resp_buf));
	test_response_count = 0;

	test_req->struct_version = EC_HOST_REQUEST_VERSION;
	test_req->checksum = 0;
	test_req->command = EC_CMD_HELLO;
	test_req->command_version = 0;
	test_req->reserved = 0;
	test_req->data_len = sizeof(*test_hello_p);
	test_hello_p->in_data = 0x11223344;

	test_pkt.send_response = test_hostcmd_respond;
	test_pkt.request = (const void *)test_req_buf;
	test_pkt.request_temp = NULL;
	test_pkt.request_max = PKT_BUFFER_SIZE;
	test_pkt.request_size = sizeof(*test_req) + sizeof(*test_hello_p);
	test_pkt.response = (void *)test_resp_buf;
	test_pkt.response_max = PKT_BUFFER_SIZE;
	test_pkt.driver_result = 0;
}

static void test_process_pending(void)
{
	if (pending_args) {
		pending_args->result = host_command_process(pending_args);
		host_send_response(pending_args);
		pending_args = NULL;
	}
}

static void test_hostcmd_send(void)
{
	test_req->checksum =
		calc_pkt_checksum(test_req_buf, test_pkt.request_size);
	host_packet_receive(&test_pkt);
	test_process_pending();
}

ZTEST(host_cmd_host_commands, test_host_packet_receive__ok)
{
	test_hostcmd_fill_default();
	test_hostcmd_send();

	zassert_equal(1, test_response_count);
	zassert_equal(
		0, calc_pkt_checksum(test_resp_buf,
				     sizeof(*test_resp) + test_resp->data_len));
	zassert_equal(EC_RES_SUCCESS, test_resp->result);
	zassert_equal(0x12243648, test_hello_r->out_data);
}

ZTEST(host_cmd_host_commands, test_host_packet_receive__too_short)
{
	test_hostcmd_fill_default();

	/* Smaller than header */
	test_pkt.request_size = sizeof(*test_req) - 4;
	test_hostcmd_send();
	zassert_equal(1, test_response_count);
	zassert_equal(EC_RES_REQUEST_TRUNCATED, test_resp->result);

	/* Smaller than expected data size */
	test_hostcmd_fill_default();
	test_pkt.request_size = sizeof(*test_req);
	test_hostcmd_send();
	zassert_equal(1, test_response_count);
	zassert_equal(EC_RES_REQUEST_TRUNCATED, test_resp->result);
}

ZTEST(host_cmd_host_commands, test_host_packet_receive__too_long)
{
	test_hostcmd_fill_default();

	/* Larger than request buffer */
	test_pkt.request_size = PKT_BUFFER_SIZE + 4;
	test_hostcmd_send();
	zassert_equal(1, test_response_count);
	zassert_equal(EC_RES_REQUEST_TRUNCATED, test_resp->result);
}

ZTEST(host_cmd_host_commands, test_host_packet_receive__driver_error)
{
	test_hostcmd_fill_default();

	test_pkt.driver_result = EC_RES_ERROR;
	test_hostcmd_send();
	zassert_equal(1, test_response_count);
	zassert_equal(EC_RES_ERROR, test_resp->result);
}

ZTEST(host_cmd_host_commands, test_host_packet_receive__invalid_command)
{
	test_hostcmd_fill_default();

	test_req->command = 0xff;
	test_hostcmd_send();
	zassert_equal(1, test_response_count);
	zassert_equal(EC_RES_INVALID_COMMAND, test_resp->result);
}

ZTEST(host_cmd_host_commands, test_host_packet_receive__wrong_command_version)
{
	test_hostcmd_fill_default();

	test_req->command_version = 1;
	test_hostcmd_send();
	zassert_equal(1, test_response_count);
	zassert_equal(EC_RES_INVALID_VERSION, test_resp->result);
}

ZTEST(host_cmd_host_commands, test_host_packet_receive__wrong_struct_version)
{
	test_hostcmd_fill_default();

	test_req->struct_version = 4;
	test_hostcmd_send();
	zassert_equal(1, test_response_count);
	zassert_equal(EC_RES_INVALID_HEADER, test_resp->result);

	test_hostcmd_fill_default();
	test_req->struct_version = 2;
	test_hostcmd_send();
	zassert_equal(1, test_response_count);
	zassert_equal(EC_RES_INVALID_HEADER, test_resp->result);
}

ZTEST(host_cmd_host_commands, test_host_packet_receive__invalid_checksum)
{
	test_hostcmd_fill_default();

	test_req->checksum =
		calc_pkt_checksum(test_req_buf, test_pkt.request_size) + 1;
	host_packet_receive(&test_pkt);
	zassert_equal(1, test_response_count);
	zassert_equal(EC_RES_INVALID_CHECKSUM, test_resp->result);
}

ZTEST(host_cmd_host_commands, test_host_packet_receive__reuse_response_buffer)
{
	struct ec_host_request *h = (struct ec_host_request *)test_resp_buf;
	struct ec_params_hello *d =
		(struct ec_params_hello *)(test_resp_buf + sizeof(*h));

	memset(test_req_buf, 0, sizeof(test_req_buf));
	memset(test_resp_buf, 0, sizeof(test_resp_buf));
	test_response_count = 0;

	h->struct_version = EC_HOST_REQUEST_VERSION;
	h->checksum = 0;
	h->command = EC_CMD_HELLO;
	h->command_version = 0;
	h->reserved = 0;
	h->data_len = sizeof(*d);
	d->in_data = 0x11223344;

	test_pkt.send_response = test_hostcmd_respond;
	test_pkt.request = (const void *)test_resp_buf;
	test_pkt.request_temp = test_req_buf;
	test_pkt.request_max = PKT_BUFFER_SIZE;
	test_pkt.request_size = sizeof(*h) + sizeof(*d);
	test_pkt.response = (void *)test_resp_buf;
	test_pkt.response_max = PKT_BUFFER_SIZE;
	test_pkt.driver_result = 0;

	h->checksum = calc_pkt_checksum(test_resp_buf, test_pkt.request_size);
	host_packet_receive(&test_pkt);
	test_process_pending();

	zassert_equal(1, test_response_count);
	zassert_equal(
		0, calc_pkt_checksum(test_resp_buf,
				     sizeof(*test_resp) + test_resp->data_len));
	zassert_equal(EC_RES_SUCCESS, test_resp->result);
	zassert_equal(0x12243648, test_hello_r->out_data);
}

ZTEST(host_cmd_host_commands, test_host_packet_respond__response_too_big)
{
	struct host_cmd_handler_args args = {
		.result = EC_RES_SUCCESS,
		.response_size = 10,
	};

	test_hostcmd_fill_default();
	pkt0 = &test_pkt;
	test_pkt.response_max = sizeof(struct ec_host_response) + 1;

	host_packet_respond(&args);

	zassert_equal(1, test_response_count);
	zassert_equal(EC_RES_RESPONSE_TOO_BIG, args.result);
	zassert_equal(0, args.response_size);
	zassert_equal(EC_RES_RESPONSE_TOO_BIG, test_resp->result);
	zassert_equal(sizeof(struct ec_host_response), test_pkt.response_size);
	pkt0 = NULL;
}

ZTEST(host_cmd_host_commands, test_host_command_received__reboot)
{
	struct host_cmd_handler_args args = {
		.command = EC_CMD_REBOOT,
		.result = EC_RES_SUCCESS,
		.send_response = test_stub_send_response,
	};

	RESET_FAKE(system_reset);
	test_stub_response_count = 0;

	host_command_received(&args);

	zassert_equal(1, system_reset_fake.call_count);
	zassert_equal(SYSTEM_RESET_HARD, system_reset_fake.arg0_history[0]);
	zassert_equal(EC_RES_ERROR, args.result);
	zassert_equal(1, test_stub_response_count);
}

ZTEST(host_cmd_host_commands, test_host_command_received__driver_error)
{
	struct host_cmd_handler_args args = {
		.command = EC_CMD_HELLO,
		.result = EC_RES_ERROR,
		.send_response = test_stub_send_response,
	};

	test_stub_response_count = 0;
	host_command_received(&args);
	zassert_equal(1, test_stub_response_count);
}

ZTEST(host_cmd_host_commands, test_host_command_received__get_comms_status_sync)
{
	struct ec_response_get_comms_status comms_resp;
	struct host_cmd_handler_args args = {
		.command = EC_CMD_GET_COMMS_STATUS,
		.version = 0,
		.result = EC_RES_SUCCESS,
		.response = &comms_resp,
		.response_max = sizeof(comms_resp),
		.send_response = test_stub_send_response,
	};

	test_stub_response_count = 0;
	host_command_received(&args);
	zassert_equal(1, test_stub_response_count);
	zassert_equal(EC_RES_SUCCESS, args.result);
	zassert_equal(sizeof(comms_resp), args.response_size);
	zassert_equal(0, comms_resp.flags);
}

ZTEST(host_cmd_host_commands, test_host_command_status_flow)
{
	struct host_cmd_handler_args args = {
		.result = EC_RES_IN_PROGRESS,
		.send_response = test_stub_send_response,
	};

	test_stub_response_count = 0;
	host_send_response(&args);
	zassert_equal(1, test_stub_response_count);
	zassert_false(host_command_in_process_ended());

	/* Complete with success and 0 response size */
	args.result = EC_RES_SUCCESS;
	args.response_size = 0;
	host_send_response(&args);
	zassert_true(host_command_in_process_ended());
	zassert_equal(EC_RES_SUCCESS, host_command_get_saved_result());

	/* Next read gives UNAVAILABLE */
	zassert_equal(EC_RES_UNAVAILABLE, host_command_get_saved_result());

	/* In progress followed by response_size != 0 sets UNAVAILABLE */
	args.result = EC_RES_IN_PROGRESS;
	host_send_response(&args);
	zassert_false(host_command_in_process_ended());

	args.result = EC_RES_SUCCESS;
	args.response_size = 10;
	host_send_response(&args);
	zassert_true(host_command_in_process_ended());
	zassert_equal(EC_RES_UNAVAILABLE, host_command_get_saved_result());
}

ZTEST(host_cmd_host_commands, test_hostcmd_clears_unused_data)
{
	int i, found_null;

	memset(test_resp_buf, 0xAA, PKT_BUFFER_SIZE);
	memset(test_req_buf, 0, sizeof(test_req_buf));
	test_response_count = 0;

	test_req->struct_version = EC_HOST_REQUEST_VERSION;
	test_req->checksum = 0;
	test_req->command = EC_CMD_GET_CHIP_INFO;
	test_req->command_version = 0;
	test_req->reserved = 0;
	test_req->data_len = 0;

	test_pkt.send_response = test_hostcmd_respond;
	test_pkt.request = (const void *)test_req_buf;
	test_pkt.request_temp = NULL;
	test_pkt.request_max = PKT_BUFFER_SIZE;
	test_pkt.request_size = sizeof(*test_req);
	test_pkt.response = (void *)test_resp_buf;
	test_pkt.response_max = PKT_BUFFER_SIZE;
	test_pkt.driver_result = 0;

	test_hostcmd_send();

	zassert_equal(1, test_response_count);
	zassert_equal(
		0, calc_pkt_checksum(test_resp_buf,
				     sizeof(*test_resp) + test_resp->data_len));
	zassert_equal(EC_RES_SUCCESS, test_resp->result);

	/* Ensure partial strings have 0s after the NULL byte */
	found_null = 0;
	for (i = 0; i < sizeof(test_chip_info_r->name); ++i) {
		if (!test_chip_info_r->name[i])
			found_null = 1;
		if (found_null)
			zassert_equal(0, test_chip_info_r->name[i]);
	}

	found_null = 0;
	for (i = 0; i < sizeof(test_chip_info_r->revision); ++i) {
		if (!test_chip_info_r->revision[i])
			found_null = 1;
		if (found_null)
			zassert_equal(0, test_chip_info_r->revision[i]);
	}

	found_null = 0;
	for (i = 0; i < sizeof(test_chip_info_r->vendor); ++i) {
		if (!test_chip_info_r->vendor[i])
			found_null = 1;
		if (found_null)
			zassert_equal(0, test_chip_info_r->vendor[i]);
	}

	/* Ensure rest of buffer after valid response is also 0 */
	for (i = test_resp->data_len + sizeof(*test_resp); i < PKT_BUFFER_SIZE;
	     ++i) {
		zassert_equal(0, test_resp_buf[i]);
	}
}
#endif /* !CONFIG_EC_HOST_CMD */

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

	/* Test 0: Verify console command shows no state initially */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "No AP fw state") != NULL,
		     "Initial state not empty: %s", outbuffer);

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
	zassert_true(strstr(outbuffer, "AP_FW 12345678") != NULL,
		     "Console command output missing state: %s", outbuffer);

	/* Test 3: Save second state (should overwrite) */
	params.state = 0x87654321;
	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);

	/* Test 4: Verify console command shows new state */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "AP_FW 87654321") != NULL,
		     "Console command output missing new state: %s", outbuffer);

	/* Test 5: Clear buffer (silent) */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens clear"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	/* Verify it's empty now */
	zassert_ok(shell_execute_cmd(shell_zephyr, "apfwscreens"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "No AP fw state") != NULL,
		     "Buffer not empty after clear! Output: %s", outbuffer);
}

#define TEST_STACK_SIZE 1024
K_THREAD_STACK_DEFINE(extra_test_stack, TEST_STACK_SIZE);
struct k_thread extra_test_thread;
K_SEM_DEFINE(test_sem, 0, 1);

static void extra_test_thread_entry(void *p1, void *p2, void *p3)
{
	while (1) {
		k_sem_take(&test_sem, K_MSEC(100));
	}
}

ZTEST(host_cmd_host_commands, test_thread_info)
{
	k_tid_t tid;
	struct ec_response_thread_info_list list;
	struct host_cmd_handler_args list_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_THREAD_INFO_LIST, 0, list);
	bool found_idle = false;
	bool found_current = false;
	bool found_test_thread = false;

	/* Create an extra thread */
	tid = k_thread_create(&extra_test_thread, extra_test_stack,
			      TEST_STACK_SIZE, extra_test_thread_entry, NULL,
			      NULL, NULL, 1, K_INHERIT_PERMS, K_NO_WAIT);
	k_thread_name_set(tid, "extra_thread");

	/* Wait for thread to start and sleep */
	k_sleep(K_MSEC(10));

	/* Call list command */
	zassert_equal(EC_RES_SUCCESS, host_command_process(&list_args));
	zassert_true(list.thread_count > 0);

	/* Verify threads */
	for (uint32_t i = 0; i < list.thread_count; i++) {
		struct ec_params_thread_info_detail p;
		struct ec_response_thread_info_detail r;
		struct host_cmd_handler_args detail_args =
			BUILD_HOST_COMMAND(EC_CMD_THREAD_INFO_DETAIL, 0, r, p);

		p.thread_id = list.thread_ids[i];
		zassert_equal(EC_RES_SUCCESS,
			      host_command_process(&detail_args));
		zassert_true(r.timestamp_us > 0, "Timestamp is 0");

		if (r.is_idle) {
			found_idle = true;
		}
		if (r.is_current) {
			found_current = true;
		}

		/* Check that flags are valid and value is != 0 for unchecked
		 * fields */
		if (r.valid_flags & EC_THREAD_INFO_DETAIL_RUNTIME_USAGE_VALID) {
			zassert_true(r.execution_time_us != 0,
				     "Execution time is 0");
		}
		if (r.valid_flags &
		    EC_THREAD_INFO_DETAIL_USAGE_ANALYSIS_VALID) {
			zassert_true(r.window_peak_us != 0, "Window peak is 0");
			zassert_true(r.window_avg_us != 0, "Window avg is 0");
		}
		if (r.valid_flags & EC_THREAD_INFO_DETAIL_PC_VALID) {
			zassert_true(r.pc != 0, "PC is 0");
		}
		if (r.valid_flags & EC_THREAD_INFO_DETAIL_LR_VALID) {
			zassert_true(r.lr != 0, "LR is 0");
		}
		if (r.valid_flags & EC_THREAD_INFO_DETAIL_SP_VALID) {
			zassert_true(r.sp != 0, "SP is 0");
		}

		/* New checks for all threads */
		zassert_true(r.entry_point > 0, "Entry point is 0");
		/* pending_on might be 0 if not blocked, so do not assert it
		 * here */
		/* Check thread_state is not dead */
		zassert_true(!(r.thread_state & 8), "Thread is dead");

		if (p.thread_id == (uint32_t)(uintptr_t)tid) {
			found_test_thread = true;
			/* Verify details of our test thread */
			zassert_true(r.valid_flags &
				     EC_THREAD_INFO_DETAIL_NAME_VALID);
			zassert_str_equal(r.name, "extra_thread");
			zassert_equal(r.prio, 1);
			zassert_equal(r.stack_size, TEST_STACK_SIZE);
			zassert_true(r.valid_flags &
				     EC_THREAD_INFO_DETAIL_STACK_VALID);
			zassert_true(r.stack_max <= r.stack_size);
			zassert_true(r.stack_cur <= r.stack_size);

			zassert_true(r.user_options & K_INHERIT_PERMS,
				     "K_INHERIT_PERMS option not set");
			zassert_true(r.timeout_us > 0, "Timeout is 0");
			zassert_true(r.timeout_us != 0xffffffff,
				     "Timeout is forever");
			zassert_equal(r.pending_on,
				      (uint32_t)(uintptr_t)&test_sem,
				      "Pending on wrong object");

			/* Additional checks for test thread */
			zassert_true(r.thread_state & 2,
				     "Thread is not pending");
		}
	}
	zassert_true(found_idle, "No idle thread found");
	zassert_true(found_current, "No current thread found");
	zassert_true(found_test_thread, "Extra test thread not found in list");

	/* Clean up thread */
	k_thread_abort(tid);
}

ZTEST_SUITE(host_cmd_host_commands, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);
