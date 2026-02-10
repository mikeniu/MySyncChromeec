/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "system_boot_time.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_pwrseq_sm.h>
#include <power_signals.h>

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#define X86_NON_DSX_FORCE_SHUTDOWN_TO_MS 50

/* Power cycling primary rail requires at least 30 ms 'off' time.
 * Skip delay during testing to avoid timing issues in the test suite.
 */
#ifdef CONFIG_TEST
#define BOARD_ATRIA_MINIMUM_POWER_DOWN_DELAY_MS 0
#else
#define BOARD_ATRIA_MINIMUM_POWER_DOWN_DELAY_MS 30
#endif

void board_ap_power_force_shutdown(void)
{
	int timeout_ms = X86_NON_DSX_FORCE_SHUTDOWN_TO_MS;

	/* De-assert PCH_PWROK and EC_PCH_SYS_PWROK */
	power_signal_set(PWR_PCH_PWROK, 0);
	power_signal_set(PWR_EC_PCH_SYS_PWROK, 0);

	/* Turn off PCH_RMSRST to meet tPCH12 */
	power_signal_set(PWR_EC_PCH_RSMRST, 1);

	/* Turn off PRIM load switch. */
	power_signal_set(PWR_EN_PP3300_A, 0);

	/* Wait RSMRST to be off. */
	while (power_signal_get(PWR_RSMRST_PWRGD) && (timeout_ms > 0)) {
		k_msleep(1);
		timeout_ms--;
	};

	if (power_signal_get(PWR_RSMRST_PWRGD))
		LOG_WRN("RSMRST_PWRGD didn't go low!  Assuming G3.");

	k_msleep(BOARD_ATRIA_MINIMUM_POWER_DOWN_DELAY_MS);
}

int board_ap_power_action_g3_entry(void *data)
{
	board_ap_power_force_shutdown();

	return 0;
}

static int board_ap_power_action_g3_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_STARTUP)) {
		power_signal_set(PWR_EN_PP3300_A, 1);

		/* Indication to soc on recovery boot */
		if (system_is_manual_recovery()) {
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(cse_early_rec_sw), 1);
		} else {
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(cse_early_rec_sw), 0);
		}

		update_ap_boot_time(ARAIL);
	}

	/* Return 0 only if power rails have been enabled  */
	return !power_signal_get(PWR_EN_PP3300_A);
}

static int board_ap_power_action_g3_exit(void *data)
{
	return 0;
}

AP_POWER_APP_STATE_DEFINE(G3, board_ap_power_action_g3_entry,
			  board_ap_power_action_g3_run,
			  board_ap_power_action_g3_exit);

/* For tests that need the code to run S0 there must be a state defined to set
 * PWR_PCH_PWROK. */
#ifdef CONFIG_TEST
static int board_ap_power_action_s0_run(void *data)
{
	power_signal_set(PWR_PCH_PWROK, 1);
	return 0;
}

AP_POWER_APP_STATE_DEFINE(S0, NULL, board_ap_power_action_s0_run, NULL);
#endif /* CONFIG_TEST */

int power_signal_external_init(void)
{
	return 0;
}

int board_power_signal_get(enum power_signal signal)
{
	switch (signal) {
	case PWR_EC_PCH_SYS_PWROK:
		return power_signal_get(PWR_PCH_PWROK);
	default:
		return -EINVAL;
	}
}

int board_power_signal_set(enum power_signal signal, int value)
{
	return 0;
}
