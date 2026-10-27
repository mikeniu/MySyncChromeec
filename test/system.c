/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test system_common.
 */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "panic.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define TEST_STATE_STEP_2 (1 << 0)
#define TEST_STATE_FAIL (1 << 1)

static int test_panic_reason_reg(void)
{
	struct panic_data test_pdata = { 0 };

	test_pdata.arch = PANIC_ARCH_CORTEX_M;

	TEST_ASSERT(panic_get_reason_reg(NULL) == 0);

	panic_set_reason_reg(NULL, PANIC_SW_DIV_ZERO);

	panic_set_reason_reg(&test_pdata, PANIC_SW_DIV_ZERO);
	TEST_ASSERT(panic_get_reason_reg(&test_pdata) == PANIC_SW_DIV_ZERO);

	panic_set_reason_reg(&test_pdata, PANIC_SW_WATCHDOG);
	TEST_ASSERT(panic_get_reason_reg(&test_pdata) == PANIC_SW_WATCHDOG);

	return EC_SUCCESS;
}

static int test_watchdog_warning_promotion(void)
{
	struct panic_data *pdata = panic_get_data();

	memset(pdata, 0, sizeof(*pdata));
	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = CONFIG_PANIC_DATA_SIZE;
	pdata->struct_version = 2;
	pdata->arch = PANIC_ARCH_CORTEX_M;
	pdata->flags = 0;

	/* Simulate registers preserved during watchdog warning */
	pdata->cm.regs[0] = 0x11111111;
	pdata->cm.regs[1] = 0x22; /* exception */
	pdata->cm.regs[2] = 0x33333333;
	pdata->cm.regs[3] = PANIC_SW_WATCHDOG_WARN; /* reason */
	pdata->cm.regs[4] = 0x44444444; /* info */
	pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_PC] = 0xdeadbeef; /* PC */

	/* Promoting watchdog warning */
	if (pdata && panic_get_reason_reg(pdata) == PANIC_SW_WATCHDOG_WARN &&
	    !(pdata->flags & PANIC_DATA_FLAG_OLD_HOSTCMD))
		panic_set_reason_reg(pdata, PANIC_SW_WATCHDOG);

	/* Verify reason was promoted but registers and metadata were preserved
	 */
	TEST_ASSERT(panic_get_reason_reg(pdata) == PANIC_SW_WATCHDOG);
	TEST_ASSERT(pdata->cm.regs[0] == 0x11111111);
	TEST_ASSERT(pdata->cm.regs[1] == 0x22);
	TEST_ASSERT(pdata->cm.regs[2] == 0x33333333);
	TEST_ASSERT(pdata->cm.regs[4] == 0x44444444);
	TEST_ASSERT(pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_PC] ==
		    0xdeadbeef);
	TEST_ASSERT(pdata->magic == PANIC_DATA_MAGIC);

	return EC_SUCCESS;
}

static int test_reboot_on_shutdown(void)
{
	struct ec_params_reboot_ec params;

	/* Fails if the system reboots unexpectedly */
	system_set_scratchpad(TEST_STATE_FAIL);

	test_chipset_on();
	crec_msleep(30);

	params.cmd = EC_REBOOT_COLD;
	params.flags = EC_REBOOT_FLAG_ON_AP_SHUTDOWN;

	TEST_EQ(test_send_host_command(EC_CMD_REBOOT_EC, 0, &params,
				       sizeof(params), NULL, 0),
		EC_SUCCESS, "%d");

	system_set_scratchpad(TEST_STATE_STEP_2);
	test_chipset_off();
	crec_msleep(30);

	/* Shouldn't reach here */
	return EC_ERROR_UNKNOWN;
}

static int test_cancel_reboot(void)
{
	struct ec_params_reboot_ec params;

	/* Fails if the system reboots unexpectedly */
	system_set_scratchpad(TEST_STATE_FAIL);

	test_chipset_on();
	crec_msleep(30);

	params.cmd = EC_REBOOT_COLD;
	params.flags = EC_REBOOT_FLAG_ON_AP_SHUTDOWN;

	TEST_EQ(test_send_host_command(EC_CMD_REBOOT_EC, 0, &params,
				       sizeof(params), NULL, 0),
		EC_SUCCESS, "%d");

	params.cmd = EC_REBOOT_CANCEL;
	params.flags = 0;

	TEST_EQ(test_send_host_command(EC_CMD_REBOOT_EC, 0, &params,
				       sizeof(params), NULL, 0),
		EC_SUCCESS, "%d");

	test_chipset_off();
	crec_msleep(30);

	return EC_SUCCESS;
}

static void run_test_step1(void)
{
	if (test_panic_reason_reg() != EC_SUCCESS)
		test_fail();

	if (test_watchdog_warning_promotion() != EC_SUCCESS)
		test_fail();

	if (test_reboot_on_shutdown() != EC_SUCCESS)
		test_fail();
}

static void run_test_step2(void)
{
	if (test_cancel_reboot() != EC_SUCCESS)
		test_fail();

	system_set_scratchpad(0);
	test_pass();
}

static void fail_and_clean_up(void)
{
	system_set_scratchpad(0);
	test_fail();
}

void run_test(int argc, const char **argv)
{
	uint32_t state = 0;

	/* The return value isn't checked here on purpose. The scratchpad file
	 * may exist from a previous run or it may be in a clean state. A
	 * previous run of this test should reset the scratchpad value to 0
	 * regardless of the final result.
	 */
	system_get_scratchpad(&state);

	test_reset();

	if (state == 0)
		run_test_step1();
	else if (state & TEST_STATE_STEP_2)
		run_test_step2();
	else if (state & TEST_STATE_FAIL)
		fail_and_clean_up();
}
