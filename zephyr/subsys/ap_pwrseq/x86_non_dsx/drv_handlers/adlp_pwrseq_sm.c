/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ap_power/ap_pwrseq_sm.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

extern void x86_non_dsx_adlp_ap_off(void);
extern int x86_non_dsx_adlp_check_pch_out_of_suspend(void);

/* Chipset specific power state machine handler */
static int x86_non_dsx_adlp_g3_run(void *data)
{
	/*
	 * Now wait for SLP_SUS_L to go high based on tPCH32. If this
	 * signal doesn't go high within 250 msec then go back to G3.
	 */
	if (x86_non_dsx_adlp_check_pch_out_of_suspend()) {
		return 0;
	}

	ap_pwrseq_post_event(ap_pwrseq_get_instance(),
			     AP_PWRSEQ_EVENT_POWER_SHUTDOWN);

	return 1;
}

AP_POWER_CHIPSET_STATE_DEFINE(G3, NULL, x86_non_dsx_adlp_g3_run, NULL);

static int x86_non_dsx_adlp_s4_run(void *data)
{
	if (!power_signal_get(PWR_DSW_PWROK) || power_signal_get(PWR_SLP_SUS)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}

	return 0;
}

AP_POWER_CHIPSET_STATE_DEFINE(S4, NULL, x86_non_dsx_adlp_s4_run, NULL);

static int x86_non_dsx_adlp_s3_run(void *data)
{
	if (!power_signal_get(PWR_DSW_PWROK) || power_signal_get(PWR_SLP_SUS)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}

	return 0;
}

static int x86_non_dsx_adlp_s3_exit(void *data)
{
	if (ap_pwrseq_sm_get_entry_state(data) < AP_POWER_STATE_S3) {
		x86_non_dsx_adlp_ap_off();
	}

	return 0;
}

AP_POWER_CHIPSET_STATE_DEFINE(S3, NULL, x86_non_dsx_adlp_s3_run,
			      x86_non_dsx_adlp_s3_exit);

static int x86_non_dsx_adlp_s0_run(void *data)
{
	int all_sys_pwrgd = power_signal_get(PWR_ALL_SYS_PWRGD);

	if (!power_signal_get(PWR_DSW_PWROK) || power_signal_get(PWR_SLP_SUS)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}

	if (power_signal_get(PWR_SLP_S3)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S3);
	}

	if (power_signal_get(PWR_VCCST_PWRGD) != all_sys_pwrgd) {
		if (all_sys_pwrgd) {
			k_msleep(AP_PWRSEQ_DT_VALUE(vccst_pwrgd_delay));
		}
		power_signal_set(PWR_VCCST_PWRGD, all_sys_pwrgd);
	}

	if (power_signal_get(PWR_PCH_PWROK) != all_sys_pwrgd) {
		if (all_sys_pwrgd) {
			k_msleep(AP_PWRSEQ_DT_VALUE(pch_pwrok_delay));
		}
		power_signal_set(PWR_PCH_PWROK, all_sys_pwrgd);
	}

	if (power_signal_get(PWR_EC_PCH_SYS_PWROK) != all_sys_pwrgd) {
		if (all_sys_pwrgd) {
			k_msleep(AP_PWRSEQ_DT_VALUE(sys_pwrok_delay));
		}
		power_signal_set(PWR_EC_PCH_SYS_PWROK, all_sys_pwrgd);
	}

	return 0;
}

static int x86_non_dsx_adlp_s0_exit(void *data)
{
	if (ap_pwrseq_sm_get_entry_state(data) < AP_POWER_STATE_S3) {
		x86_non_dsx_adlp_ap_off();
	}

	return 0;
}

AP_POWER_CHIPSET_STATE_DEFINE(S0, NULL, x86_non_dsx_adlp_s0_run,
			      x86_non_dsx_adlp_s0_exit);

#if CONFIG_AP_PWRSEQ_S0IX
static int x86_non_dsx_adlp_s0ix_run(void *data)
{
	/* System in S0 only if SLP_S0 and SLP_S3 are de-asserted */
	if (power_signals_off(IN_PCH_SLP_S0) &&
	    power_signals_off(IN_PCH_SLP_S3)) {
		/* TODO: Make sure ap reset handling is done
		 * before leaving S0ix.
		 */
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S0);
	} else if (!power_signals_on(POWER_SIGNAL_MASK(PWR_DSW_PWROK)) ||
		   power_signals_on(POWER_SIGNAL_MASK(PWR_SLP_SUS))) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}

	return 0;
}

AP_POWER_CHIPSET_SUB_STATE_DEFINE(S0ix, NULL, x86_non_dsx_adlp_s0ix_run, NULL,
				  S0);
#endif /* CONFIG_AP_PWRSEQ_S0IX */
