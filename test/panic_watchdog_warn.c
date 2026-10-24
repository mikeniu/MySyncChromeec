/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "panic.h"
#include "test_util.h"

test_static int test_get_panic_info_watchdog_warn(void)
{
	struct panic_data *pdata = panic_get_data();
	struct host_cmd_handler_args args = {0};
	uint8_t response_buf[sizeof(struct panic_data)];

	/* Setup panic data with PANIC_SW_WATCHDOG_WARN */
	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = sizeof(struct panic_data);
	pdata->reason = PANIC_SW_WATCHDOG_WARN;

	args.command = EC_CMD_GET_PANIC_INFO;
	args.version = 0;
	args.response = response_buf;
	args.response_max = sizeof(response_buf);
	args.response_size = 0;

	/* The host command should return SUCCESS but with response_size 0 */
	TEST_EQ(host_command_process(&args), EC_RES_SUCCESS, "%d");
	TEST_EQ(args.response_size, 0, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	RUN_TEST(test_get_panic_info_watchdog_warn);
	test_print_result();
}
