/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __X86_COMMON_PWRSEQ_H__
#define __X86_COMMON_PWRSEQ_H__

#include <zephyr/logging/log.h>

#include <ap_power/ap_power_interface.h>
#include <ap_power_override_functions.h>
#include <power_signals.h>
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

#endif /* __X86_COMMON_PWRSEQ_H__ */
