/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#ifdef SECTION_IS_RW
#include "fpsensor/fpsensor_state_driver.h"

constexpr uint8_t kCanaryByte = 0x5a;

static bool canary_found_before_pre_init;

/*
 * In core/cortex-m/init.S, the reset handler zeroes .bss and then calls
 * __libc_init_array (which executes __attribute__((constructor)) functions)
 * before jumping to main().
 *
 * Running this check in .init_array lets RW inspect fp_buffer before main()
 * calls RW's system_pre_init(), which would otherwise clear .ahb4 during RW
 * boot and mask a failure of system_reset() to clear fp_buffer before
 * resetting into RO. Before RW's system_pre_init() runs, fp_buffer must
 * already have been cleared across the reset (by RW's system_reset() for .ahb4
 * NOLOAD on STM32H7, or by .bss initialization in init.S on other boards).
 */
__attribute__((constructor)) static void check_canary_before_pre_init(void)
{
	size_t canary_count = std::count(std::begin(fp_buffer),
					 std::end(fp_buffer), kCanaryByte);

	canary_found_before_pre_init = (canary_count > 0);
}

test_static int test_fp_buffer_before_reboot(void)
{
	/* Ensure fp_buffer begins zeroed. */
	TEST_ASSERT_MEMSET(fp_buffer, 0, sizeof(fp_buffer));

	/* Poison fp_buffer with canary values. */
	ccprints("RW: Poisoning fp_buffer with 0x%02x...", kCanaryByte);
	cflush();

	std::memset(fp_buffer, kCanaryByte, sizeof(fp_buffer));

	/* Verify write succeeded. */
	TEST_ASSERT_MEMSET(fp_buffer, kCanaryByte, sizeof(fp_buffer));

	return EC_SUCCESS;
}

test_static int test_fp_buffer_after_reboot(void)
{
	TEST_ASSERT(!canary_found_before_pre_init);

	/* Verify fp_buffer is zeroed and canary didn't survive. */
	TEST_ASSERT_MEMSET(fp_buffer, 0, sizeof(fp_buffer));

	return EC_SUCCESS;
}

test_static void run_test_step1(void)
{
	ccprints("Step 1: Poison fp_buffer in RW");
	cflush();

	RUN_TEST(test_fp_buffer_before_reboot);

	if (test_get_error_count()) {
		test_reboot_to_next_step(TEST_STATE_FAILED);
	} else {
		test_reboot_to_next_step(TEST_STATE_STEP_2);
	}
}

test_static void run_test_step2(void)
{
	ccprints("Step 2: Verify fp_buffer cleared in RW");
	cflush();

	RUN_TEST(test_fp_buffer_after_reboot);

	if (test_get_error_count()) {
		test_reboot_to_next_step(TEST_STATE_FAILED);
	} else {
		test_reboot_to_next_step(TEST_STATE_PASSED);
	}
}

void test_run_step(uint32_t state)
{
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1)) {
		run_test_step1();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		run_test_step2();
	}
}
#endif /* SECTION_IS_RW */

extern "C" int task_test(void *unused)
{
	if (IS_ENABLED(SECTION_IS_RW))
		test_run_multistep();
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	crec_msleep(100); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
