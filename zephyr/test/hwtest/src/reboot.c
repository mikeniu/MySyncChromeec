/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "multistep_test.h"
#include "system.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(reboot_hw_test, LOG_LEVEL_INF);

static void trigger_reboot(void)
{
	/* Trigger reboot. */
	LOG_INF("Triggering reboot...");
	/*
	 * We need to flush the console before rebooting, otherwise we might
	 * miss the log message.
	 */
	cflush();
	system_reset(SYSTEM_RESET_HARD);

	zassert_unreachable();
}

static void check_reboot(void)
{
	uint32_t flags = system_get_reset_flags();

	LOG_INF("Reset flags: 0x%x", flags);

	/*
	 * If BBRAM is working, we expect EC_RESET_FLAG_HARD.
	 * If BBRAM is NOT working, we expect the hardware reset cause to map
	 * to EC_RESET_FLAG_SOFT (e.g. NVIC_SystemReset) or
	 * EC_RESET_FLAG_WATCHDOG (e.g. watchdog reset), depending on the chip.
	 */
	zassert_true(flags & (EC_RESET_FLAG_HARD | EC_RESET_FLAG_SOFT |
			      EC_RESET_FLAG_WATCHDOG),
		     "Reboot flags not set: 0x%x", flags);
}

static void (*test_steps[])(void) = { trigger_reboot, check_reboot };

MULTISTEP_TEST(reboot, test_steps);
