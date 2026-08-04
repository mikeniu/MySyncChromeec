/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SUBSYS_AP_PWRSEQ_INCLUDE_X86_COMMON_PWRSEQ_H_
#define PLATFORM_EC_ZEPHYR_SUBSYS_AP_PWRSEQ_INCLUDE_X86_COMMON_PWRSEQ_H_

#include <ap_power/ap_power_interface.h>
#include <ap_power_override_functions.h>
#include <power_signals.h>
#include <system_boot_time.h>
#include <x86_power_signals.h>

#ifndef CONFIG_AP_PWRSEQ_DRIVER
/* This encapsulates the attributes of the state machine */
struct pwrseq_context {
	/* On power-on start boot up sequence */
	enum power_states_ndsx power_state;
};
#endif

/*
 * Returns true if all signals in mask are valid.
 * This is only done for virtual wire signals.
 */
static inline bool signals_valid(power_signal_mask_t signals)
{
	/* LCOV_EXCL_START - TODO: cover invalid VW signals via state machine */
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S3)
	if ((signals & POWER_SIGNAL_MASK(PWR_SLP_S3)) &&
	    power_signal_get(PWR_SLP_S3) < 0)
		return false;
#endif
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S4)
	if ((signals & POWER_SIGNAL_MASK(PWR_SLP_S4)) &&
	    power_signal_get(PWR_SLP_S4) < 0)
		return false;
#endif
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S5)
	if ((signals & POWER_SIGNAL_MASK(PWR_SLP_S5)) &&
	    power_signal_get(PWR_SLP_S5) < 0)
		return false;
#endif
	/* LCOV_EXCL_STOP */
	return true;
}

static inline bool signals_valid_and_on(power_signal_mask_t signals)
{
	return signals_valid(signals) && power_signals_on(signals);
}

static inline bool signals_valid_and_off(power_signal_mask_t signals)
{
	return signals_valid(signals) && power_signals_off(signals);
}

/* Check RSMRST is fine to move from S5 to higher state */
static inline int rsmrst_power_is_good(void)
{
	/* TODO: Check if this is still intact */
	return power_signal_get(PWR_RSMRST_PWRGD);
}

/* Handling RSMRST signal is mostly common across x86 chipsets */
static inline void rsmrst_pass_thru_handler(void)
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
			power_signal_set(PWR_EC_PCH_RSMRST, 0);
			update_ap_boot_time(RSMRST);
		}
	} else {
		power_signal_set(PWR_EC_PCH_RSMRST, 1);
	}
}

#endif /* PLATFORM_EC_ZEPHYR_SUBSYS_AP_PWRSEQ_INCLUDE_X86_COMMON_PWRSEQ_H_ */
