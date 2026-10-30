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

#ifdef CONFIG_AIC_PWRSEQ_FAULT_INJECTION
#include "gpio.h"
#include "hooks.h"
#include "power_button.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include <ap_power/ap_pwrseq.h>
#endif

LOG_MODULE_REGISTER(aic_pwrseq_board, LOG_LEVEL_INF);

static void board_ap_power_shutdown(void)
{
	int timeout_ms = 100;

	power_signal_set(PWR_PCH_PWROK, 0);
	power_signal_set(PWR_EC_PCH_SYS_PWROK, 0);
#ifdef CONFIG_AIC_PWRSEQ_FAULT_INJECTION
	/*
	 * Deliberately violate tPCH12 (min 400 ns from RSMRST# assertion to
	 * primary rail drop) by dropping PP5000_A 50 us before asserting
	 * RSMRST#.
	 */
	power_signal_set(PWR_EN_PP5000_A, 0);
	k_busy_wait(50);
	power_signal_set(PWR_EC_PCH_RSMRST, 1);
#else
	power_signal_set(PWR_EC_PCH_RSMRST, 1);
	power_signal_set(PWR_EN_PP5000_A, 0);
#endif

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

#ifdef CONFIG_AIC_PWRSEQ_FAULT_INJECTION
static int board_ap_power_action_s0_exit(void *data)
{
	if (ap_pwrseq_sm_get_entry_state(data) == AP_POWER_STATE_S3) {
		/*
		 * Violate tCPU22 (> 50 ms max delay from SLP_S3# assert to
		 * PLT_PWROK deassert) by holding in S0 exit for 60 ms.
		 */
		k_msleep(60);
	}
	return 0;
}

AP_POWER_APP_STATE_DEFINE(S0, NULL, NULL, board_ap_power_action_s0_exit);

static void fault_pwrbtn_release(void)
{
	gpio_set_level(GPIO_PCH_PWRBTN_L, 1);
}
DECLARE_DEFERRED(fault_pwrbtn_release);

static void fault_pwrbtn_change(void)
{
	if (power_button_is_pressed() &&
	    ap_pwrseq_get_current_state(ap_pwrseq_get_instance()) ==
		    AP_POWER_STATE_S0) {
		/*
		 * Truncate S0 PWRBTN pulse to 5 ms to violate tPCH43 (min 18
		 * ms).
		 */
		hook_call_deferred(&fault_pwrbtn_release_data,
				   5 * USEC_PER_MSEC);
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, fault_pwrbtn_change, HOOK_PRIO_LAST);
#endif

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
