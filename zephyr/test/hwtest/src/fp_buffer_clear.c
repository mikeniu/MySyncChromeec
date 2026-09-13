/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "multistep_test.h"
#include "system.h"
#include "task.h"

#include <stdint.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(fp_buffer_clear_hw_test, LOG_LEVEL_INF);

#ifdef CONFIG_PLATFORM_EC_FINGERPRINT
#include "fpsensor/fpsensor_state_driver.h"

#define CANARY_BYTE 0x5a

static void test_fp_buffer_before_reboot(void)
{
	LOG_INF("Step 1: Poison fp_buffer in RW");
	cflush();

	/* Ensure fp_buffer begins zeroed. */
	for (size_t i = 0; i < sizeof(fp_buffer); i++) {
		zassert_equal(fp_buffer[i], 0,
			      "fp_buffer not zeroed at index %zu", i);
	}

	/* Poison fp_buffer with canary values. */
	LOG_INF("RW: Poisoning fp_buffer with 0x%02x...", CANARY_BYTE);
	cflush();

	memset(fp_buffer, CANARY_BYTE, sizeof(fp_buffer));

	/* Verify write succeeded. */
	for (size_t i = 0; i < sizeof(fp_buffer); i++) {
		zassert_equal(fp_buffer[i], CANARY_BYTE,
			      "fp_buffer write did not succeed at index %zu",
			      i);
	}

	system_reset(SYSTEM_RESET_HARD);
}

static void test_fp_buffer_after_reboot(void)
{
	LOG_INF("Step 2: Verify fp_buffer cleared in RW");
	cflush();

	/* Verify fp_buffer is zeroed and canary didn't survive. */
	for (size_t i = 0; i < sizeof(fp_buffer); i++) {
		zassert_equal(fp_buffer[i], 0,
			      "fp_buffer canary survived at index %zu", i);
	}
}

static void (*test_steps[])(void) = {
	test_fp_buffer_before_reboot,
	test_fp_buffer_after_reboot,
};

MULTISTEP_TEST(fp_buffer_clear, test_steps)
#endif /* CONFIG_PLATFORM_EC_FINGERPRINT */
