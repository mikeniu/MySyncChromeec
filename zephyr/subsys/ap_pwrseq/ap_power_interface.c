/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ap_reset_log.h"

#include <ap_power/ap_power_interface.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

#ifndef CONFIG_AP_PWRSEQ_DRIVER
bool ap_power_in_state(enum ap_power_state_mask state_mask)
{
	int need_mask = 0;

	switch (pwr_sm_get_state()) {
	case SYS_POWER_STATE_UNINIT:
		LOG_WRN("%s: init not yet complete; AP state is unknown",
			__func__);
		return false;
	case SYS_POWER_STATE_G3:
		need_mask = AP_POWER_STATE_HARD_OFF;
		break;
	case SYS_POWER_STATE_G3S5:
	case SYS_POWER_STATE_S5G3:
		/*
		 * In between hard and soft off states.  Match only if caller
		 * will accept both.
		 */
		need_mask = AP_POWER_STATE_HARD_OFF | AP_POWER_STATE_SOFT_OFF;
		break;
	case SYS_POWER_STATE_S5:
		need_mask = AP_POWER_STATE_SOFT_OFF;
		break;
	case SYS_POWER_STATE_S5S4:
	case SYS_POWER_STATE_S4S5:
		need_mask = AP_POWER_STATE_SOFT_OFF | AP_POWER_STATE_SUSPEND;
		break;
	case SYS_POWER_STATE_S4:
	case SYS_POWER_STATE_S4S3:
	case SYS_POWER_STATE_S3S4:
	case SYS_POWER_STATE_S3:
		need_mask = AP_POWER_STATE_SUSPEND;
		break;
	case SYS_POWER_STATE_S3S0:
	case SYS_POWER_STATE_S0S3:
		need_mask = AP_POWER_STATE_SUSPEND | AP_POWER_STATE_ON;
		break;
	case SYS_POWER_STATE_S0:
		need_mask = AP_POWER_STATE_ON;
		break;
#if CONFIG_AP_PWRSEQ_S0IX
	case SYS_POWER_STATE_S0ixS0:
	case SYS_POWER_STATE_S0S0ix:
		need_mask = AP_POWER_STATE_ON | AP_POWER_STATE_STANDBY;
		break;
	case SYS_POWER_STATE_S0ix:
		need_mask = AP_POWER_STATE_STANDBY;
		break;
#endif
	}
	/* Return non-zero if all needed bits are present */
	return (state_mask & need_mask) == need_mask;
}

bool ap_power_in_or_transitioning_to_state(enum ap_power_state_mask state_mask)
{
	switch (pwr_sm_get_state()) {
	case SYS_POWER_STATE_UNINIT:
		LOG_WRN("%s: init not yet complete; AP state is unknown",
			__func__);
		return 0;
	case SYS_POWER_STATE_G3:
	case SYS_POWER_STATE_S5G3:
		return state_mask & AP_POWER_STATE_HARD_OFF;
	case SYS_POWER_STATE_S5:
	case SYS_POWER_STATE_G3S5:
	case SYS_POWER_STATE_S4S5:
		return state_mask & AP_POWER_STATE_SOFT_OFF;
	case SYS_POWER_STATE_S3:
	case SYS_POWER_STATE_S4:
	case SYS_POWER_STATE_S3S4:
	case SYS_POWER_STATE_S5S4:
	case SYS_POWER_STATE_S4S3:
	case SYS_POWER_STATE_S0S3:
		return state_mask & AP_POWER_STATE_SUSPEND;
#if CONFIG_AP_PWRSEQ_S0IX
	case SYS_POWER_STATE_S0ix:
	case SYS_POWER_STATE_S0S0ix:
		return state_mask & AP_POWER_STATE_STANDBY;
#endif
	case SYS_POWER_STATE_S0:
	case SYS_POWER_STATE_S3S0:
#if CONFIG_AP_PWRSEQ_S0IX
	case SYS_POWER_STATE_S0ixS0:
#endif
		return state_mask & AP_POWER_STATE_ON;
	}
	/* Unknown power state; return false. */
	return 0;
}

void ap_power_exit_hardoff(void)
{
	enum power_states_ndsx power_state;

	/*
	 * If not in the soft-off state, hard-off state, or headed there,
	 * nothing to do.
	 */
	power_state = pwr_sm_get_state();
	if (power_state != SYS_POWER_STATE_G3 &&
	    power_state != SYS_POWER_STATE_S5G3 &&
	    power_state != SYS_POWER_STATE_S5)
		return;
	request_start_from_g3();
}
#else
bool ap_power_in_state(enum ap_power_state_mask state_mask)
{
	/*
	 * PWRSEQ_DRIVER will only expose stable power states (transitions
	 * occur automatically and are not visible to API consumers), so
	 * return ap_power_in_or_transitioning_to_state since it is
	 * equivalent to this function.
	 */
	return ap_power_in_or_transitioning_to_state(state_mask);
}

bool ap_power_in_or_transitioning_to_state(enum ap_power_state_mask state_mask)
{
	const struct device *dev = ap_pwrseq_get_instance();

	switch (ap_pwrseq_get_current_state(dev)) {
	case AP_POWER_STATE_G3:
		return state_mask & AP_POWER_STATE_HARD_OFF;
	case AP_POWER_STATE_S5:
		return state_mask & AP_POWER_STATE_SOFT_OFF;
	case AP_POWER_STATE_S3:
	case AP_POWER_STATE_S4:
		return state_mask & AP_POWER_STATE_SUSPEND;
#if CONFIG_AP_PWRSEQ_S0IX
	case AP_POWER_STATE_S0ix:
		return state_mask & AP_POWER_STATE_STANDBY;
#endif
	case AP_POWER_STATE_S0:
		return state_mask & AP_POWER_STATE_ON;
	default:
		break;
	}
	/* Unknown power state; return false. */
	return 0;
}

void ap_power_exit_hardoff(void)
{
	const struct device *dev = ap_pwrseq_get_instance();
	enum ap_pwrseq_state power_state;

	ap_pwrseq_state_lock(dev);
	/*
	 * If not in the soft-off state, hard-off state, or headed there,
	 * nothing to do.
	 */
	power_state = ap_pwrseq_get_current_state(dev);
	if (power_state == AP_POWER_STATE_G3 ||
	    power_state == AP_POWER_STATE_S5) {
		request_start_from_g3();
	}

	ap_pwrseq_state_unlock(dev);
}

void ap_pwrseq_task_start(void)
{
	const struct device *dev = ap_pwrseq_get_instance();

	/*
	 * Start the AP power sequencing state machine, seeding it with the
	 * current hardware power state so it resumes from the correct state
	 * rather than always starting from an assumed initial state.
	 * If chipset_pwr_seq_get_state returns AP_POWER_STATE_G3, the AP
	 * power sequence driver will fire <ARCH/CHIPSET/BOARD>_g3_entry
	 * function where each implementation can set power signals to correct
	 * state.
	 */
	ap_pwrseq_start(dev, chipset_pwr_seq_get_state());
}
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

void ap_power_force_shutdown(enum ap_power_shutdown_reason reason)
{
	/* This prevents force shutdown if debug mode is enabled */
	if (ap_power_in_debug_mode()) {
		LOG_WRN("debug_mode is enabled, preventing force shutdown");
		return;
	}

	report_ap_reset((enum chipset_shutdown_reason)reason);
#ifdef CONFIG_AP_PWRSEQ_DRIVER
	ap_pwrseq_post_event(ap_pwrseq_get_instance(),
			     AP_PWRSEQ_EVENT_POWER_SHUTDOWN);
#else
	board_ap_power_force_shutdown();
#endif
}

void ap_power_reset(enum ap_power_shutdown_reason reason)
{
	/*
	 * Irrespective of cold_reset value, always toggle SYS_RESET_L to
	 * perform an AP reset. RCIN# which was used earlier to trigger
	 * a warm reset is known to not work in certain cases where the CPU
	 * is in a bad state (crbug.com/721853).
	 *
	 * The EC cannot control warm vs cold reset of the AP using
	 * SYS_RESET_L; it's more of a request.
	 */
	LOG_DBG("%s: %d", __func__, reason);

	/*
	 * Toggling SYS_RESET_L will not have any impact when it's already
	 * low (i,e. AP is in reset state).
	 */
	if (power_signal_get(PWR_SYS_RST)) {
		LOG_DBG("Chipset is in reset state");
		return;
	}

	report_ap_reset((enum chipset_shutdown_reason)reason);

	power_signal_set(PWR_SYS_RST, 1);
	/*
	 * Debounce time for SYS_RESET_L is 16 ms. Wait twice that period
	 * to be safe.
	 */
	k_msleep(AP_PWRSEQ_DT_VALUE(sys_reset_delay));
	power_signal_set(PWR_SYS_RST, 0);
	ap_power_ev_send_callbacks(AP_POWER_RESET);
}

#ifdef CONFIG_AP_PWRSEQ_DEBUG_MODE_COMMAND
static bool in_debug_mode;

bool ap_power_in_debug_mode(void)
{
	return in_debug_mode;
}

/*
 * Intel debugger puts SOC in boot halt mode for step debugging,
 * during this time EC may lose Sx lines, Adding this console
 * command to avoid force shutdown.
 */
static int disable_force_shutdown(const struct shell *sh, size_t argc,
				  char **argv)
{
	if (argc > 1) {
		if (!strcmp(argv[1], "enable")) {
			in_debug_mode = true;
		} else if (!strcmp(argv[1], "disable")) {
			in_debug_mode = false;
		} else {
			return EC_ERROR_PARAM1;
		}
	}
	LOG_INF("debug_mode = %s", (in_debug_mode ? "enabled" : "disabled"));

	return EC_SUCCESS;
}

SHELL_CMD_REGISTER(debug_mode, NULL, "[enable|disable]",
		   disable_force_shutdown);
#else
bool ap_power_in_debug_mode(void)
{
	return false;
}
#endif /* CONFIG_AP_PWRSEQ_DEBUG_MODE_COMMAND */
