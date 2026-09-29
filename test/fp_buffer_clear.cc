/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#include <cstdint>
#include <cstring>

#ifdef SECTION_IS_RW
#include "fpsensor/fpsensor_state_driver.h"

#define CANARY_BYTE 0x5a

static bool canary_found_before_pre_init;

__attribute__((constructor)) static void check_canary_before_pre_init(void)
{
	size_t canary_count = 0;

	for (size_t i = 0; i < sizeof(fp_buffer); i++) {
		if (fp_buffer[i] == CANARY_BYTE) {
			canary_count++;
		}
	}

	/*
	 * Before system_pre_init() runs in RW, fp_buffer must already have been
	 * cleared across the reset (e.g., by system_reset() before entering an
	 * older RO, by RO's system_pre_init(), or by .bss initialization).
	 * Because RO may use the beginning of .ahb4 for its UART buffers before
	 * jumping to RW, check that the canary pattern did not survive across
	 * the buffer.
	 */
	canary_found_before_pre_init = (canary_count > sizeof(fp_buffer) / 2);
}

test_static int test_fp_buffer_before_reboot(void)
{
	/* Ensure fp_buffer begins zeroed. */
	for (size_t i = 0; i < sizeof(fp_buffer); i++) {
		TEST_ASSERT(fp_buffer[i] == 0);
	}

	/* Poison fp_buffer with canary values. */
	ccprints("RW: Poisoning fp_buffer with 0x%02x...", CANARY_BYTE);
	cflush();

	std::memset(fp_buffer, CANARY_BYTE, sizeof(fp_buffer));

	/* Verify write succeeded. */
	for (size_t i = 0; i < sizeof(fp_buffer); i++) {
		TEST_ASSERT(fp_buffer[i] == CANARY_BYTE);
	}

	return EC_SUCCESS;
}

test_static int test_fp_buffer_after_reboot(void)
{
	TEST_ASSERT(!canary_found_before_pre_init);

	/* Verify fp_buffer is zeroed and canary didn't survive. */
	for (size_t i = 0; i < sizeof(fp_buffer); i++) {
		TEST_ASSERT(fp_buffer[i] == 0);
	}

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
