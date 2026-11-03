/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "gpio_signal.h"
#include "include/system.h"
#include "system_boot_time.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_pwrseq_sm.h>
#include <power_signals.h>

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#define X86_NON_DSX_FORCE_SHUTDOWN_TO_MS 50

/* Power cycling primary rail requires at least 30 ms 'off' time */
#define BOARD_OCELOT_MINIMUM_POWER_DOWN_DELAY_MS 30

static void board_ap_power_shutdown(void)
{
	int timeout_ms = X86_NON_DSX_FORCE_SHUTDOWN_TO_MS;

	/* Turn off PCH_RMSRST to meet tPCH12 */
	power_signal_set(PWR_EC_PCH_RSMRST, 1);

	/* Disable all control gpio. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_edp_bl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_codec_mute_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tpad_en), 0);

	/* Turn off PRIM load switch. */
	power_signal_set(PWR_EN_PP3300_A, 0);

	/* Wait RSMRST to be off. */
	while (power_signal_get(PWR_RSMRST_PWRGD) && (timeout_ms > 0)) {
		k_msleep(1);
		timeout_ms--;
	};

	if (power_signal_get(PWR_RSMRST_PWRGD))
		LOG_WRN("RSMRST_PWRGD didn't go low! Assuming G3.");

	k_msleep(BOARD_OCELOT_MINIMUM_POWER_DOWN_DELAY_MS);
}

int board_ap_power_action_g3_entry(void *data)
{
	board_ap_power_shutdown();

	return 0;
}

static int board_ap_power_action_g3_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_SHUTDOWN)) {
		board_ap_power_shutdown();
		return 1;
	}

	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_STARTUP)) {
		LOG_DBG("Turning on EN_S5_RAILS");
		power_signal_set(PWR_EN_PP3300_A, 1);

		/* Enable all control gpio. */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_edp_bl), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_codec_mute_odl),
				1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tpad_en), 1);

		update_ap_boot_time(ARAIL);
	}

	/* Return 0 only if power rails have been enabled  */
	return !power_signal_get(PWR_EN_PP3300_A);
}

AP_POWER_APP_STATE_DEFINE(G3, board_ap_power_action_g3_entry,
			  board_ap_power_action_g3_run, NULL);

int power_signal_external_init(void)
{
	return 0;
}

int board_power_signal_get(enum power_signal signal)
{
	return -EINVAL;
}

int board_power_signal_set(enum power_signal signal, int value)
{
	return -EINVAL;
}
