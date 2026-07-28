/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

bool chipset_is_prim_power_good(void)
{
	return power_signal_get(PWR_RSMRST_PWRGD);
}

bool chipset_is_vw_power_good(void)
{
	return chipset_is_prim_power_good() &&
	       !power_signal_get(PWR_EC_PCH_RSMRST);
}

bool chipset_is_all_power_good(void)
{
	return chipset_is_vw_power_good() &&
	       power_signal_get(PWR_ALL_SYS_PWRGD) &&
	       power_signal_get(PWR_PCH_PWROK) &&
	       power_signal_get(PWR_EC_PCH_SYS_PWROK);
}

void x86_non_dsx_mtl_ap_off(void)
{
	power_signal_set(PWR_PCH_PWROK, 0);
	power_signal_set(PWR_EC_PCH_SYS_PWROK, 0);
}
