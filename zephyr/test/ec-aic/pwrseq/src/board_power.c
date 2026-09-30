/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_pwrseq_sm.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power_interface.h>
#include <power_signals.h>
#include <x86_power_signals.h>

LOG_MODULE_REGISTER(aic_pwrseq_board, LOG_LEVEL_INF);

static void board_ap_power_shutdown(void)
{
	int timeout_ms = 100;

	power_signal_set(PWR_PCH_PWROK, 0);
	power_signal_set(PWR_EC_PCH_SYS_PWROK, 0);
	power_signal_set(PWR_EC_PCH_RSMRST, 1);
	power_signal_set(PWR_EN_PP5000_A, 0);

	while (power_signal_get(PWR_RSMRST_PWRGD) && (timeout_ms > 0)) {
		k_msleep(1);
		timeout_ms--;
	}
}

static int board_ap_power_action_g3_entry(void *data)
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
		power_signal_set(PWR_EN_PP5000_A, 1);
	}

	if (!power_signal_get(PWR_EN_PP5000_A)) {
		return 1;
	}

	return 0;
}

AP_POWER_APP_STATE_DEFINE(G3, board_ap_power_action_g3_entry,
			  board_ap_power_action_g3_run, NULL);

int power_signal_external_init(void)
{
	return 0;
}

int board_power_signal_get(enum power_signal signal)
{
	switch (signal) {
	case PWR_SYS_RST:
		return 0;
	default:
		LOG_WRN("Unknown board signal get: %d", signal);
		return 1;
	}
}

int board_power_signal_set(enum power_signal signal, int value)
{
	return 0;
}

void ap_power_set_active_wake_mask(void)
{
}
