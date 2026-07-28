/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_pwrseq.h"
#include "ap_power/ap_pwrseq_sm.h"
#include "ap_reset_log.h"
#include "system_boot_time.h"
#include "x86_non_dsx_common_pwrseq_sm_handler.h"
#include "zephyr_console_shim.h"

#include <zephyr/init.h>

#include <atomic.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

/*
 * Flags, may be set/cleared from other threads.
 */
enum {
	S5_INACTIVE_TIMER_RUNNING,
	START_FROM_G3,
	FLAGS_MAX,
};
static ATOMIC_DEFINE(flags, FLAGS_MAX);

void x86_non_dsx_start_from_g3(void)
{
	atomic_set_bit(flags, START_FROM_G3);
}

const char *const pwr_sm_get_state_name(enum ap_pwrseq_state state)
{
	return ap_pwrseq_get_state_str(state);
}

void start_from_g3_timer_handler(struct k_timer *timer)
{
	if (atomic_test_bit(flags, START_FROM_G3)) {
		ap_pwrseq_post_event(ap_pwrseq_get_instance(),
				     AP_PWRSEQ_EVENT_POWER_STARTUP);
	}
}

void s5_inactive_timer_handler(struct k_timer *timer)
{
	if (atomic_test_bit(flags, S5_INACTIVE_TIMER_RUNNING)) {
		ap_pwrseq_post_event(ap_pwrseq_get_instance(),
				     AP_PWRSEQ_EVENT_POWER_TIMEOUT);
	}
}

static int x86_non_dsx_g3_entry(void *data)
{
	if (!atomic_test_bit(flags, START_FROM_G3)) {
		return 0;
	}

	start_start_from_g3_timer();

	return 0;
}

static int x86_non_dsx_g3_run(void *data)
{
	/*
	 * If the START_FROM_G3 flag is set, begin starting
	 * the AP. There may be a delay set, so only start
	 * after that delay.
	 */
	if (!atomic_test_bit(flags, START_FROM_G3)) {
		return 0;
	}

	if (!board_ap_power_is_startup_ok()) {
		LOG_INF("Start from G3 inhibited"
			" by !is_startup_ok");
		ap_pwrseq_post_event(ap_pwrseq_get_instance(),
				     AP_PWRSEQ_EVENT_POWER_SHUTDOWN);
		atomic_clear_bit(flags, START_FROM_G3);
		return 0;
	}

	if (get_remaining_start_from_g3_timer()) {
		return 0;
	}

	/*
	 * At this point all power rails and power signals are already checked
	 * by application and chipset state action handlers, it is safe to move
	 * forward to S5.
	 */
	return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S5);
}

static int x86_non_dsx_g3_exit(void *data)
{
	atomic_clear_bit(flags, START_FROM_G3);

	return 0;
}

AP_POWER_ARCH_STATE_DEFINE(G3, x86_non_dsx_g3_entry, x86_non_dsx_g3_run,
			   x86_non_dsx_g3_exit);

static int x86_non_dsx_s5_entry(void *data)
{
	/* This prevents force shutdown if debug mode is enabled */
	if (ap_power_in_debug_mode()) {
		return 0;
	}

	atomic_set_bit(flags, S5_INACTIVE_TIMER_RUNNING);
	start_s5_inactive_timer();

	return 0;
}

static int x86_non_dsx_s5_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_SHUTDOWN)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}
	/*
	 * At this point, lower level action handlers of state machine should
	 * have already checked that required power rails are OK.
	 */
	rsmrst_pass_thru_handler();
	if (!power_signal_get(PWR_EC_PCH_RSMRST)) {
		if (signals_valid_and_off(IN_PCH_SLP_S5)) {
			return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S4);
		}
	}
	/* This prevents force shutdown if debug mode is enabled */
	if (ap_power_in_debug_mode()) {
		LOG_WRN("debug_mode is enabled, preventing G3 transition");
		return 0;
	}
	/* S5 inactivity timeout, go to G3 */
	if (get_remaining_s5_inactive_timer() == 0) {
		/* Timer is expired */
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}

	return 0;
}

static int x86_non_dsx_s5_exit(void *data)
{
	if (atomic_test_and_clear_bit(flags, S5_INACTIVE_TIMER_RUNNING)) {
		stop_s5_inactive_timer();
	}

	return 0;
}

AP_POWER_ARCH_STATE_DEFINE(S5, x86_non_dsx_s5_entry, x86_non_dsx_s5_run,
			   x86_non_dsx_s5_exit);

static int x86_non_dsx_s4_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_SHUTDOWN)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}
	if (power_signal_get(PWR_RSMRST_PWRGD) == 0 ||
	    signals_valid_and_on(IN_PCH_SLP_S5)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S5);
	}

	if (signals_valid_and_off(IN_PCH_SLP_S4)) {
#if CONFIG_AP_PWRSEQ_S0IX
		/*
		 * Clearing the S0ix flag on the path to S0
		 * to handle any reset conditions.
		 */
		ap_power_reset_host_sleep_state();
#endif
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S3);
	}

	return 0;
}

AP_POWER_ARCH_STATE_DEFINE(S4, NULL, x86_non_dsx_s4_run, NULL);

static int x86_non_dsx_s3_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_SHUTDOWN)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}
	if (power_signal_get(PWR_RSMRST_PWRGD) == 0 ||
	    signals_valid_and_on(IN_PCH_SLP_S4)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S4);
	}

	if (signals_valid_and_on(IN_PCH_SLP_S3)) {
		return 0;
	}

	/* All the power rails must be stable */
	if (power_signal_get(PWR_ALL_SYS_PWRGD)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S0);
	}

	return 0;
}

AP_POWER_ARCH_STATE_DEFINE(S3, NULL, x86_non_dsx_s3_run, NULL);

static int x86_non_dsx_s0_entry(void *data)
{
	disable_sleep(SLEEP_MASK_AP_RUN);

	return 0;
}

static int x86_non_dsx_s0_run(void *data)
{
	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_SHUTDOWN)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}
	if (signals_valid_and_on(IN_PCH_SLP_S3)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S3);
	}
#if CONFIG_AP_PWRSEQ_S0IX
	if (ap_power_sleep_get_notify() == AP_POWER_SLEEP_SUSPEND &&
	    power_signal_get(PWR_SLP_S0) != 0) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S0ix);
	} else if (ap_power_sleep_get_notify() == AP_POWER_SLEEP_RESUME) {
		ap_power_sleep_notify_transition(AP_POWER_SLEEP_RESUME);
	} else if (power_signal_get(PWR_SLP_S0) == 0) {
		disable_sleep(SLEEP_MASK_AP_RUN);
	}
#endif

	return 0;
}

static int x86_non_dsx_s0_exit(void *data)
{
	enable_sleep(SLEEP_MASK_AP_RUN);

	return 0;
}

AP_POWER_ARCH_STATE_DEFINE(S0, x86_non_dsx_s0_entry, x86_non_dsx_s0_run,
			   x86_non_dsx_s0_exit);
