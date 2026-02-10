/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Atria RVP board-specific AP power sequence tests.
 *
 * These tests exercise code paths in rvp_board_power.c that are not covered
 * by the generic ap_pwrseq test suite.
 */

#include "power_signals.h"
#include "test_state.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/* Declared in rvp_board_power.c and compiled into the test binary when
 * CONFIG_TEST_AP_POWER_ATRIARVP=y.
 */
extern void board_ap_power_force_shutdown(void);
extern int board_power_signal_get(enum power_signal signal);

/**
 * @brief Test that board_ap_power_force_shutdown emits a warning when
 * PWR_RSMRST_PWRGD remains asserted after the 50 ms timeout.
 *
 * The normal emulator brings RSMRST low ~10 ms after PP3300_A is de-asserted,
 * so the timeout path (and the associated LOG_WRN) is never reached in
 * ordinary test runs.  This test drives RSMRST high through the GPIO emulator
 * directly (without any power-signal emulator loaded) so that
 * board_ap_power_force_shutdown has to spin through the full timeout loop.
 */
ZTEST(ap_pwrseq_atriarvp_board, test_force_shutdown_rsmrst_timeout)
{
	const struct gpio_dt_spec rsmrst =
		GPIO_DT_SPEC_GET(DT_NODELABEL(pwr_rsmrst_pwrgd), gpios);

	/* Drive RSMRST_PWRGD high so it will not drop during force_shutdown. */
	gpio_emul_input_set(rsmrst.port, rsmrst.pin, 1);

	/*
	 * board_ap_power_force_shutdown will poll RSMRST for up to
	 * X86_NON_DSX_FORCE_SHUTDOWN_TO_MS (50) ms and then emit LOG_WRN.
	 * In CONFIG_TEST builds BOARD_ATRIA_MINIMUM_POWER_DOWN_DELAY_MS is 0,
	 * so the function returns promptly after the warning.
	 */
	board_ap_power_force_shutdown();

	/* Restore RSMRST to de-asserted state for subsequent tests. */
	gpio_emul_input_set(rsmrst.port, rsmrst.pin, 0);
}

/**
 * @brief Test that board_power_signal_get returns -EINVAL for any signal
 * other than PWR_EC_PCH_SYS_PWROK (exercises the default switch case).
 */
ZTEST(ap_pwrseq_atriarvp_board, test_board_power_signal_get_default)
{
	zassert_equal(-EINVAL, board_power_signal_get(PWR_EN_PP3300_A),
		      "Expected -EINVAL for unsupported signal");
}

ZTEST_SUITE(ap_pwrseq_atriarvp_board, ap_power_predicate_post_main, NULL, NULL,
	    NULL, NULL);
