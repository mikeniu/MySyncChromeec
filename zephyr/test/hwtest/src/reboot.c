/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(reboot_hw_test, LOG_LEVEL_INF);

ZTEST_SUITE(reboot, NULL, NULL, NULL, NULL, NULL);

ZTEST(reboot, test_reboot)
{
	uint32_t flags = system_get_reset_flags();

	LOG_INF("Reset flags: 0x%x", flags);

	/*
	 * We don't want to use multistep_test (which uses scratchpad) because
	 * we don't want to assume that BBRAM works.
	 * Instead, we check the reset flags.
	 *
	 * If BBRAM is working, we expect EC_RESET_FLAG_HARD.
	 * If BBRAM is NOT working, we expect the hardware reset cause to map
	 * to EC_RESET_FLAG_SOFT (e.g. NVIC_SystemReset) or
	 * EC_RESET_FLAG_WATCHDOG (e.g. watchdog reset), depending on the chip.
	 */
	if (flags & (EC_RESET_FLAG_HARD | EC_RESET_FLAG_SOFT |
		     EC_RESET_FLAG_WATCHDOG)) {
		/* We rebooted! */
		return;
	}

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

/*
 * If the test shell is enabled, the test will be run once by a test runner.
 * The test will cause reboot/crash, which means we need to run it again.
 */
IF_ENABLED(CONFIG_ZTEST_SHELL, (
	static int reboot_test_init(void)
	{
		if (system_get_reset_flags() &
		    (EC_RESET_FLAG_HARD | EC_RESET_FLAG_SOFT |
		     EC_RESET_FLAG_WATCHDOG)) {
			ztest_run_test_suite(reboot, false, 1, 1, NULL);
		}
		return 0;
	}
	SYS_INIT(reboot_test_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
))
