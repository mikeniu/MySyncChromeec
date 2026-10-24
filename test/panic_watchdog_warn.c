/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "host_command.h"
#include "panic.h"
#include "test_util.h"
#include "software_panic.h"
#include "task.h"
#include "sysjump.h"

struct mock_memory {
	uint8_t pad[1024];
	struct jump_data jdata;
	struct panic_data pdata;
};

static struct mock_memory mock_mem = {
	.pdata = {
		.magic = PANIC_DATA_MAGIC,
		.struct_size = sizeof(struct panic_data),
	},
};

#undef PANIC_DATA_PTR
#define PANIC_DATA_PTR (&mock_mem.pdata)
#undef CONFIG_PANIC_DATA_BASE
#define CONFIG_PANIC_DATA_BASE ((uintptr_t)&mock_mem.pdata)

#undef CONFIG_SOFTWARE_PANIC
#define CONFIG_SOFTWARE_PANIC

#undef CONFIG_DEBUG_ASSERT_REBOOTS
#undef CONFIG_CMD_CRASH

void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception);
void software_panic(uint32_t reason, uint32_t info) __attribute__((noreturn));

/* Dummy implementation for panic_data_print */
void panic_data_print(const struct panic_data *pdata)
{
}

#include "../common/panic_output.c"

static uint32_t mock_reason;

void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception)
{
	if (reason)
		*reason = mock_reason;
	if (info)
		*info = 0;
	if (exception)
		*exception = 0;
}

void software_panic(uint32_t reason, uint32_t info)
{
	while (1);
}

test_static int test_get_panic_info_watchdog_warn(void)
{
	struct panic_data *pdata = panic_get_data();
	struct host_cmd_handler_args args = {0};
	uint8_t response_buf[sizeof(struct panic_data)];

	/* Setup panic data with PANIC_SW_WATCHDOG_WARN */
	if (pdata) {
		pdata->magic = PANIC_DATA_MAGIC;
		pdata->struct_size = sizeof(struct panic_data);
	}
	mock_reason = PANIC_SW_WATCHDOG_WARN;

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

int task_test(void *data)
{
	test_reset();
	RUN_TEST(test_get_panic_info_watchdog_warn);
	test_print_result();
	return EC_SUCCESS;
}

void run_test(void)
{
	task_wake(TASK_ID_TEST);
}
