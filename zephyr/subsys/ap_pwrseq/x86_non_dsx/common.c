/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "system_boot_time.h"
#include "x86_non_dsx_common_pwrseq_sm_handler.h"
#include "zephyr_console_shim.h"

#include <zephyr/init.h>
/* Delay in seconds when starting from G3 */
static uint32_t start_from_g3_delay_s;
LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);
/*
 * Functions required to be implemented by state machine handler file
 */
extern void x86_non_dsx_start_from_g3(void);
extern void start_from_g3_timer_handler(struct k_timer *timer);
extern void s5_inactive_timer_handler(struct k_timer *timer);
K_TIMER_DEFINE(start_from_g3_timer, start_from_g3_timer_handler, NULL);
K_TIMER_DEFINE(s5_inactive_timer, s5_inactive_timer_handler, NULL);

void set_start_from_g3_delay_seconds(uint32_t d_time)
{
	start_from_g3_delay_s = d_time;
}

void start_start_from_g3_timer(void)
{
	k_timer_start(&start_from_g3_timer, K_SECONDS(start_from_g3_delay_s),
		      K_NO_WAIT);
	start_from_g3_delay_s = 0;
}

uint32_t get_remaining_start_from_g3_timer(void)
{
	return k_timer_remaining_get(&start_from_g3_timer);
}

void start_s5_inactive_timer(void)
{
	k_timer_start(&s5_inactive_timer,
		      K_SECONDS(AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout)),
		      K_NO_WAIT);
}

uint32_t get_remaining_s5_inactive_timer(void)
{
	return k_timer_remaining_get(&s5_inactive_timer);
}

void stop_s5_inactive_timer(void)
{
	k_timer_stop(&s5_inactive_timer);
}

void request_start_from_g3(void)
{
	LOG_INF("Request start from G3");
	if (!board_ap_power_is_startup_ok()) {
		LOG_INF("Start from G3 inhibited"
			" by !is_startup_ok");
		return;
	}
	/*
	 * If in S5, restart the timer to give the CPU more time
	 * to respond to a power button press (which is presumably
	 * why we are being called). This avoids having the S5
	 * inactivity timer expiring before the AP can process
	 * the power button press and start up.
	 */
	if (ap_power_in_state(AP_POWER_STATE_SOFT_OFF)) {
		start_s5_inactive_timer();
		return;
	}
	x86_non_dsx_start_from_g3();
	if (ap_power_in_state(AP_POWER_STATE_HARD_OFF)) {
		start_start_from_g3_timer();
	}
}
int rsmrst_power_is_good(void)
{
	/* TODO: Check if this is still intact */
	return power_signal_get(PWR_RSMRST_PWRGD);
}
/* Handling RSMRST signal is mostly common across x86 chipsets */
void rsmrst_pass_thru_handler(void)
{
	/* Handle RSMRST passthrough */
	/* TODO: Add additional conditions for RSMRST handling */
	if (power_signal_get(PWR_RSMRST_PWRGD)) {
		if (power_signal_get(PWR_EC_PCH_RSMRST)) {
			/*
			 * Delay `PWR_EC_PCH_RSMRST` de-assertion for at least
			 * `rsmrst_delay` after detecting that power wells are
			 * stable.
			 */
			k_msleep(AP_PWRSEQ_DT_VALUE(rsmrst_delay));
			LOG_DBG("Deasserting PWR_EC_PCH_RSMRST");
			power_signal_set(PWR_EC_PCH_RSMRST, 0);
			update_ap_boot_time(RSMRST);
		}
	} else {
		power_signal_set(PWR_EC_PCH_RSMRST, 1);
	}
}
