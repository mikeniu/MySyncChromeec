/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_reset_log.h"
#include "system_boot_time.h"
#include "zephyr_console_shim.h"

#include <zephyr/init.h>

#include <atomic.h>
#ifndef CONFIG_AP_PWRSEQ_DRIVER
#include <x86_non_dsx_common_pwrseq_sm_handler.h>
#else
#include "ap_power/ap_pwrseq.h"
#include "ap_power/ap_pwrseq_sm.h"
#include "x86_non_dsx_common_pwrseq_sm_handler.h"
#include "zephyr_console_shim.h"
#endif

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

#ifndef CONFIG_AP_PWRSEQ_DRIVER
static K_KERNEL_STACK_DEFINE(pwrseq_thread_stack, CONFIG_AP_PWRSEQ_STACK_SIZE);
static struct k_thread pwrseq_thread_data;
static struct pwrseq_context pwrseq_ctx = {
	.power_state = SYS_POWER_STATE_UNINIT,
};
static struct k_sem pwrseq_sem;

/**
 * @brief power_state names for debug
 */
static const char *const pwrsm_dbg[] = {
	[SYS_POWER_STATE_UNINIT] = "Unknown",
	[SYS_POWER_STATE_G3] = "G3",
	[SYS_POWER_STATE_S5] = "S5",
	[SYS_POWER_STATE_S4] = "S4",
	[SYS_POWER_STATE_S3] = "S3",
#if CONFIG_AP_PWRSEQ_S0IX
	[SYS_POWER_STATE_S0ix] = "S0ix",
#endif
	[SYS_POWER_STATE_S0] = "S0",
	[SYS_POWER_STATE_G3S5] = "G3S5",
	[SYS_POWER_STATE_S5S4] = "S5S4",
	[SYS_POWER_STATE_S4S3] = "S4S3",
	[SYS_POWER_STATE_S3S0] = "S3S0",
	[SYS_POWER_STATE_S5G3] = "S5G3",
	[SYS_POWER_STATE_S4S5] = "S4S5",
	[SYS_POWER_STATE_S3S4] = "S3S4",
	[SYS_POWER_STATE_S0S3] = "S0S3",
#if CONFIG_AP_PWRSEQ_S0IX
	[SYS_POWER_STATE_S0ixS0] = "S0ixS0",
	[SYS_POWER_STATE_S0S0ix] = "S0S0ix",
#endif
};

enum power_states_ndsx pwr_sm_get_state(void)
{
	return pwrseq_ctx.power_state;
}

const char *const pwr_sm_get_state_name(enum power_states_ndsx state)
{
	return pwrsm_dbg[state];
}

void pwr_sm_set_state(enum power_states_ndsx new_state)
{
	/* Add locking mechanism if multiple thread can update it */
	LOG_DBG("Power state: %s --> %s",
		pwr_sm_get_state_name(pwrseq_ctx.power_state),
		pwr_sm_get_state_name(new_state));
	pwrseq_ctx.power_state = new_state;
}

void ap_pwrseq_wake(void)
{
	k_sem_give(&pwrseq_sem);
}

void s5_inactive_timer_handler(struct k_timer *timer)
{
	if (atomic_test_bit(flags, S5_INACTIVE_TIMER_RUNNING)) {
		ap_pwrseq_wake();
	}
}

void start_from_g3_timer_handler(struct k_timer *timer)
{
	if (atomic_test_bit(flags, START_FROM_G3)) {
		ap_pwrseq_wake();
	}
}

static void shutdown_and_notify(enum ap_power_shutdown_reason reason)
{
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	ap_power_force_shutdown(reason);
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN_COMPLETE);
}

const char *const pwr_sm_get_state_name(enum ap_pwrseq_state state)
{
	return ap_pwrseq_get_state_str(state);
}

/* Common power sequencing */
static int common_pwr_sm_run(int state)
{
	switch (state) {
	case SYS_POWER_STATE_G3: {
		/* Callback event `AP_POWER_HARD_OFF` event must be generated
		 * only when entering G3 from any higher power state.
		 */
		static bool gen_hard_off_evt = false;

		if (gen_hard_off_evt) {
			/* Notify power event before we enter G3 */
			ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
			gen_hard_off_evt = false;
		}
		/*
		 * If the START_FROM_G3 flag is set, begin starting
		 * the AP. There may be a delay set, so only start
		 * after that delay.
		 */
		if (atomic_test_bit(flags, START_FROM_G3) &&
		    get_remaining_start_from_g3_timer() == 0) {
			atomic_clear_bit(flags, START_FROM_G3);
			if (!board_ap_power_is_startup_ok()) {
				LOG_INF("Start from G3 inhibited"
					" by !is_startup_ok");
				break;
			}
			gen_hard_off_evt = true;
			return SYS_POWER_STATE_G3S5;
		}

		break;
	}
	case SYS_POWER_STATE_G3S5:
		if ((power_get_signals() & PWRSEQ_G3S5_UP_SIGNAL) ==
		    PWRSEQ_G3S5_UP_VALUE)
			return SYS_POWER_STATE_S5;
		else
			return SYS_POWER_STATE_S5G3;

	case SYS_POWER_STATE_S5:
		/* In S5 make sure no more signal lost */
		/* If A-rails are stable then move to higher state */
		if (board_ap_power_check_power_rails_enabled() &&
		    rsmrst_power_is_good()) {
			/* rsmrst is intact */
			rsmrst_pass_thru_handler();
			if (signals_valid_and_off(IN_PCH_SLP_S5)) {
				stop_s5_inactive_timer();
				/* Clear the timer running flag */
				atomic_clear_bit(flags,
						 S5_INACTIVE_TIMER_RUNNING);
				/* Clear any request to exit hard-off */
				atomic_clear_bit(flags, START_FROM_G3);
				LOG_INF("Clearing request to exit G3");
				return SYS_POWER_STATE_S5S4;
			}
		}

		if (ap_power_in_debug_mode()) {
			LOG_WRN("debug_mode is enabled, preventing shutdown");
			break;
		}
		/*
		 * S5 state has an inactivity timer, so moving
		 * to S5G3 (where the power rails are turned off) is
		 * delayed for some time, usually ~10 seconds or so.
		 * The purpose of this delay is:
		 *  - to handle AP initiated cold boot, where the AP
		 *    will go to S5 for a short time and then restart.
		 *  - give time for the power button to be pressed,
		 *    which may set the START_FROM_G3 flag.
		 */
		if (!atomic_test_and_set_bit(flags,
					     S5_INACTIVE_TIMER_RUNNING)) {
			/*
			 * Test and set timer running flag.
			 * If it was 0, then the timer wasn't running
			 * and it is started (and the flag is set),
			 * otherwise it is already set, so no change.
			 */
			start_s5_inactive_timer();
		} else if (get_remaining_s5_inactive_timer() == 0) {
			/* Either timer expired or its duration was set to `0`,
			 * in either case we can move forward. */
			atomic_clear_bit(flags, S5_INACTIVE_TIMER_RUNNING);
			return SYS_POWER_STATE_S5G3;
		}
		break;

	case SYS_POWER_STATE_S5G3:
		if (atomic_test_bit(flags, START_FROM_G3)) {
			start_start_from_g3_timer();
		}
		/* Nofity power event after we remove power rails */
		ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
		return SYS_POWER_STATE_G3;

	case SYS_POWER_STATE_S5S4:
		/* Check if the PCH has come out of suspend state */
		if (rsmrst_power_is_good()) {
			LOG_DBG("RSMRST is ok");
			return SYS_POWER_STATE_S4;
		}
		LOG_DBG("RSMRST is not ok");
		return SYS_POWER_STATE_S5;

	case SYS_POWER_STATE_S4:
		if (signals_valid_and_on(IN_PCH_SLP_S5) ||
		    !rsmrst_power_is_good())
			return SYS_POWER_STATE_S4S5;
		else if (signals_valid_and_off(IN_PCH_SLP_S4))
			return SYS_POWER_STATE_S4S3;

		break;

	case SYS_POWER_STATE_S4S3:
		if (!chipset_is_prim_power_good()) {
			/* Required rail went away */
			shutdown_and_notify(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		}

		/* Notify power event that rails are up */
		ap_power_ev_send_callbacks(AP_POWER_STARTUP);

#if CONFIG_AP_PWRSEQ_S0IX
		/*
		 * Clearing the S0ix flag on the path to S0
		 * to handle any reset conditions.
		 */
		ap_power_reset_host_sleep_state();
#endif
		return SYS_POWER_STATE_S3;

	case SYS_POWER_STATE_S3:
		/* AP is out of suspend to RAM */
		if (!rsmrst_power_is_good()) {
			LOG_WRN("RSMRST is not GOOD");
			return SYS_POWER_STATE_S3S4;
		}
		if (!chipset_is_prim_power_good()) {
			/* Required rail went away, go straight to S5 */
			shutdown_and_notify(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		} else if (signals_valid_and_off(IN_PCH_SLP_S3))
			return SYS_POWER_STATE_S3S0;
		else if (signals_valid_and_on(IN_PCH_SLP_S4))
			return SYS_POWER_STATE_S3S4;

		break;

	case SYS_POWER_STATE_S3S0:
		if (!chipset_is_prim_power_good()) {
			shutdown_and_notify(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		}
		if (!rsmrst_power_is_good()) {
			return SYS_POWER_STATE_S3;
		}

		/* All the power rails must be stable */
		if (power_signal_get(PWR_ALL_SYS_PWRGD)) {
			/*
			 * Disable idle task deep sleep when in S0.
			 */
			disable_sleep(SLEEP_MASK_AP_RUN);
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
			/* Notify power event before resume */
			ap_power_ev_send_callbacks(AP_POWER_RESUME_INIT);
#endif
			/* Notify power event rails are up */
			ap_power_ev_send_callbacks(AP_POWER_RESUME);
			return SYS_POWER_STATE_S0;
		}
		break;

#if CONFIG_AP_PWRSEQ_S0IX
	case SYS_POWER_STATE_S0ix:
		if (power_signal_get(PWR_SYS_RST)) {
			ap_power_reset_host_sleep_state();
			return SYS_POWER_STATE_S0ixS0;
		}
		/* System in S0 only if SLP_S0 and SLP_S3 are de-asserted */
		if ((power_signals_off(IN_PCH_SLP_S0) &&
		     signals_valid_and_off(IN_PCH_SLP_S3) &&
		     ap_power_sleep_get_notify() == AP_POWER_SLEEP_RESUME)) {
			return SYS_POWER_STATE_S0ixS0;
		} else if (!chipset_is_all_power_good())
			return SYS_POWER_STATE_S0;

		break;

	case SYS_POWER_STATE_S0S0ix:
		/*
		 * Check sleep state and notify listeners of S0ix suspend if
		 * HC already set sleep suspend state.
		 */
		ap_power_sleep_notify_transition(AP_POWER_SLEEP_SUSPEND);
		ap_power_ev_send_callbacks(AP_POWER_S0IX_SUSPEND);

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S0ix.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
		ap_power_ev_send_callbacks(AP_POWER_SUSPEND_COMPLETE);
#endif

		return SYS_POWER_STATE_S0ix;

	case SYS_POWER_STATE_S0ixS0:
		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		ap_power_ev_send_callbacks(AP_POWER_S0IX_RESUME
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
					   | AP_POWER_RESUME_INIT
#endif
		);

		return SYS_POWER_STATE_S0;

#endif /* CONFIG_AP_PWRSEQ_S0IX */

	case SYS_POWER_STATE_S0:
		if (!chipset_is_prim_power_good()) {
			shutdown_and_notify(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		} else if (signals_valid_and_on(IN_PCH_SLP_S3)) {
			return SYS_POWER_STATE_S0S3;

#if CONFIG_AP_PWRSEQ_S0IX
			/*
			 * SLP_S0 may assert in system idle scenario without a
			 * kernel freeze call. This may cause interrupt storm
			 * since there is no freeze/unfreeze of threads/process
			 * in the idle scenario. Ignore the SLP_S0 assertions in
			 * idle scenario by checking the host sleep state.
			 */
		} else if ((ap_power_sleep_get_notify() ==
				    AP_POWER_SLEEP_SUSPEND &&
			    power_signals_on(IN_PCH_SLP_S0)) &&
			   !power_signal_get(PWR_SYS_RST)) {
			return SYS_POWER_STATE_S0S0ix;
		} else if (ap_power_sleep_get_notify() ==
			   AP_POWER_SLEEP_RESUME) {
			ap_power_sleep_notify_transition(AP_POWER_SLEEP_RESUME);
#endif /* CONFIG_AP_PWRSEQ_S0IX */
		}

		break;

	case SYS_POWER_STATE_S4S5:
		/* Notify power event before we remove power rails */
		ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);

		/*
		 * If support controlling power of wifi/WWAN/BT devices
		 * add handling here.
		 */
		ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN_COMPLETE);

		/* Always enter into S5 state. The S5 state is required to
		 * correctly handle global resets which have a bit of delay
		 * while the SLP_Sx_L signals are asserted then deasserted.
		 */
		/* TODO */
		/* power_s5_up = 0; */

		return SYS_POWER_STATE_S5;

	case SYS_POWER_STATE_S3S4:
		return SYS_POWER_STATE_S4;

	case SYS_POWER_STATE_S0S3:
		/* Notify power event before we remove power rails */
		ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
		/* Notify power event after suspend */
		ap_power_ev_send_callbacks(AP_POWER_SUSPEND_COMPLETE);
#endif

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

#if CONFIG_AP_PWRSEQ_S0IX
		/* Re-initialize S0ix flag */
		ap_power_reset_host_sleep_state();
#endif

		return SYS_POWER_STATE_S3;

	default:
		break;
	}

	return state;
}

/*
 * Determine the current CPU state and ensure it
 * is matching what is required.
 */
static void pwr_seq_set_initial_state(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	/* Determine current state using chipset specific handler */
	enum power_states_ndsx state = chipset_pwr_seq_get_state();

	/*
	 * Not in warm boot, but CPU is not shutdown.
	 */
	if (((reset_flags & EC_RESET_FLAG_SYSJUMP) == 0) &&
	    (state != SYS_POWER_STATE_G3)) {
		ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
		state = SYS_POWER_STATE_G3;
	}
	pwr_sm_set_state(state);
}

static void pwrseq_loop_thread(void *p1, void *p2, void *p3)
{
	enum power_states_ndsx curr_state, new_state;
	power_signal_mask_t this_in_signals;
	power_signal_mask_t last_in_signals = 0;
	enum power_states_ndsx last_state = -1;

	/*
	 * Let clients know that the AP power state is now
	 * initialized and ready.
	 */
	ap_power_ev_send_callbacks(AP_POWER_INITIALIZED);
	while (1) {
		curr_state = pwr_sm_get_state();

		/*
		 * In order to prevent repeated console spam, only print the
		 * current power state if something has actually changed.  It's
		 * possible that one of the power signals goes away briefly and
		 * comes back by the time we update our signals.
		 */
		this_in_signals = power_get_signals();

		if (this_in_signals != last_in_signals ||
		    curr_state != last_state) {
			LOG_INF("power state %d = %s, in 0x%04x", curr_state,
				pwr_sm_get_state_name(curr_state),
				this_in_signals);
			last_in_signals = this_in_signals;
			last_state = curr_state;
		}

		/* Run chipset specific state machine */
		new_state = chipset_pwr_sm_run(curr_state);

		/*
		 * Run common power state machine
		 * if the state has changed in chipset state
		 * machine then skip running common state
		 * machine
		 */
		if (curr_state == new_state)
			new_state = common_pwr_sm_run(curr_state);

		if (curr_state != new_state) {
			pwr_sm_set_state(new_state);
			ap_power_set_active_wake_mask();
		} else {
			/*
			 * No state transition, we can go to sleep and wait
			 * for any event to wake us up.
			 */
			k_sem_take(&pwrseq_sem, K_FOREVER);
		}
	}
}

BUILD_ASSERT(
	EC_TASK_PRIORITY(EC_TASK_AP_PWRSEQ_PRIO) ==
		CONFIG_AP_PWRSEQ_THREAD_PRIORITY,
	"EC_TASK_AP_PWRSEQ_PRIO does not match CONFIG_AP_PWRSEQ_THREAD_PRIORITY.");

static inline void create_pwrseq_thread(void)
{
	k_thread_create(&pwrseq_thread_data, pwrseq_thread_stack,
			K_KERNEL_STACK_SIZEOF(pwrseq_thread_stack),
			(k_thread_entry_t)pwrseq_loop_thread, NULL, NULL, NULL,
			CONFIG_AP_PWRSEQ_THREAD_PRIORITY, 0,
			IS_ENABLED(CONFIG_AP_PWRSEQ_AUTOSTART) ? K_NO_WAIT :
								 K_FOREVER);

	k_thread_name_set(&pwrseq_thread_data, "pwrseq_task");
}

void ap_pwrseq_task_start(void)
{
	if (!IS_ENABLED(CONFIG_AP_PWRSEQ_AUTOSTART)) {
		k_thread_start(&pwrseq_thread_data);
	}
}

static void init_pwr_seq_state(void)
{
	atomic_clear_bit(flags, START_FROM_G3);
	/*
	 * The state of the CPU needs to be determined now
	 * so that init routines can check the state of
	 * the CPU.
	 */
	pwr_seq_set_initial_state();
}

/* Initialize power sequence system state */
static int pwrseq_init(void)
{
	LOG_INF("Pwrseq Init");

	k_sem_init(&pwrseq_sem, 0, 1);
	/* Initialize signal handlers */
	power_signal_init();
	LOG_DBG("Init pwr seq state");
	init_pwr_seq_state();
	/* Create power sequence state handler core function thread */
	create_pwrseq_thread();
	return 0;
}

k_tid_t get_ap_pwrseq_thread(void)
{
	return &pwrseq_thread_data;
}

/*
 * The initialization must occur after system I/O initialization that
 * the signals depend upon, such as GPIO, ADC etc.
 */
SYS_INIT(pwrseq_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#else

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
#endif /* CONFIG_AP_PWRSEQ_DRIVER */
