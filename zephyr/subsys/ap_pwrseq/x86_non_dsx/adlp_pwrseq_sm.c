/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

bool chipset_is_prim_power_good(void)
{
	return !power_signal_get(PWR_SLP_SUS) &&
	       power_signal_get(PWR_DSW_PWROK);
}

bool chipset_is_vw_power_good(void)
{
	return chipset_is_prim_power_good() &&
	       power_signal_get(PWR_RSMRST_PWRGD) &&
	       !power_signal_get(PWR_EC_PCH_RSMRST);
}

bool chipset_is_all_power_good(void)
{
	return chipset_is_vw_power_good() &&
	       power_signal_get(PWR_ALL_SYS_PWRGD);
}

/* The wait time is ~150 msec, allow for safety margin. */
#define IN_PCH_SLP_SUS_WAIT_TIME_MS 250

int x86_non_dsx_adlp_check_pch_out_of_suspend(void)
{
	int ret;
	/*
	 * Wait for SLP_SUS_L to go high based on tPCH32. If this
	 * signal doesn't go high within 250 msec then go back to G3.
	 */
	ret = power_wait_mask_signals_timeout(IN_PCH_SLP_SUS, 0,
					      IN_PCH_SLP_SUS_WAIT_TIME_MS);
	if (ret == 0) {
		LOG_DBG("SLP_SUS now %d", power_signal_get(PWR_SLP_SUS));
		return 1;
	}
	LOG_ERR("wait SLP_SUS deassertion timeout");
	return 0; /* timeout */
}

void x86_non_dsx_adlp_ap_off(void)
{
	power_signal_set(PWR_VCCST_PWRGD, 0);
	power_signal_set(PWR_PCH_PWROK, 0);
	power_signal_set(PWR_EC_PCH_SYS_PWROK, 0);
}
