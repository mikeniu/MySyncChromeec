/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * USBC TypeC SNK with DebugAccessory State Machine
 *   See Figure 4-13 in Release 2.1 of USB Type-C Spec.
 */
#include <logging/log.h>
#include "usbc_tc_sm.h"
#include "usbc_tcpc.h"

LOG_MODULE_REGISTER(usbc_tc_sm, CONFIG_USBC_TC_SM_LOG_LEVEL);


/*****************************************************************************
 * Forward Declarations
 */
/* Full list of TypeC states. This is indexed by usbc_tc_state */
static const struct usbc_state tc_states[];


/*****************************************************************************
 * Debug
 */
/* List of human readable state names for console debugging */
static __unused __const_data const char * const tc_state_names[] = {
#if (CONFIG_USBC_TC_SM_LOG_LEVEL != LOG_LEVEL_NONE)
	[TC_DISABLED] = "Disabled",
	[TC_ERROR_RECOVERY] = "ErrorRecovery",
	[TC_UNATTACHED_SNK] = "Unattached.SNK",
	[TC_ATTACH_WAIT_SNK] = "AttachWait.SNK",
	[TC_ATTACHED_SNK] = "Attached.SNK",
#if (USBC_TC_INCLUDES_DBGACC_OPTION)
	[TC_DEBUG_ACCESSORY_SNK] = "DebugAccessory.SNK"
#endif /* USBC_TC_INCLUDES_DBGACC_OPTION */

	/* Super States */
	[TC_CC_OPEN] = "SS:CC_OPEN",

	[TC_STATE_COUNT] = "",
#endif /* CONFIG_USBC_TC_SM_LOG_LEVEL != LOG_LEVEL_NONE */
};

/* Debug log level - higher number == more log */
#ifdef CONFIG_USBC_PD_DEBUG_LEVEL
static const enum debug_level tc_debug_level = CONFIG_USBC_PD_DEBUG_LEVEL;
#else
static enum debug_level tc_debug_level = DEBUG_LEVEL_1;
#endif /* CONFIG_USBC_PD_DEBUG_LEVEL */

void tc_set_debug_level(enum debug_level debug_level)
{
	if (!IS_ENABLED(CONFIG_USBC_PD_DEBUG_LEVEL))
		tc_debug_level = debug_level;
}


/*****************************************************************************
 * Private helper functions
 */
/* Set the TypeC state machine to a new state. */
static void set_state_tc(
	const struct usbc_port_data *port_data,
	const enum usbc_tc_state new_state)
{
	assert(port_data->port_thread == k_current_get());

	smf_set_state(SMF_CTX(port_data->tc), &tc_states[new_state]);
}

/* Get the current TypeC state. */
static enum usbc_tc_state get_state_tc(
	const struct usbc_port_data *port_data)
{
	const struct usbc_state *ctx_current = port_data->tc.ctx.current;

	/* Default to returning TC_STATE_COUNT if no state has been set */
	if (ctx_current == NULL)
		return TC_STATE_COUNT;
	else
		return ctx_current - &tc_states[0];
}


/*****************************************************************************
 * Public Functions
 */
const char *tc_get_current_state(
	struct usbc_port_data *port_data)
{
	if (CONFIG_USBC_TC_SM_LOG_LEVEL != LOG_LEVEL_NONE)
		return tc_state_names[get_state_tc(port_data)];
	else
		return "";
}

uint32_t tc_get_flags(
	struct usbc_port_data *port_data)
{
	return port_data->tc.flags;
}

bool tc_is_attached_snk(
	struct usbc_port_data *port_data)
{
	return IS_ATTACHED_SNK(port_data);
}

bool tc_is_port_enabled(struct usbc_port_data *port_data)
{
	/*
	 * Checking get_state_tc(port) from another task isn't safe since it
	 * can return TC_DISABLED before tc_cc_open_entry and tc_disabled_entry
	 * are complete. So check TC_FLAGS_SUSPENDED instead.
	 */
	return !TC_CHK_FLAG(port_data, TC_FLAGS_SUSPENDED);
}

uint8_t tc_get_polarity(struct usbc_port_data *port_data)
{
	return port_data->tc.polarity;
}

/*
 * Depending on the load on the processor and the tasks running
 * it can take a while for the task associated with this port
 * to run.  So build in 1ms delays, for up to 300ms, to wait for
 * the suspend to actually happen.
 */
#define SUSPEND_SLEEP_DELAY	1
#define SUSPEND_SLEEP_RETRIES	300

void tc_set_suspend(
	struct usbc_port_data *port_data,
	int suspend)
{
	if (tc_is_port_enabled(port_data) == !suspend)
		return;

	/* Track if we are suspended or not */
	if (suspend) {
		int wait = 0;

		TC_SET_FLAG(port_data, TC_FLAGS_REQUEST_SUSPEND);

		/*
		 * Avoid deadlock when running from task
		 * which we are going to suspend
		 */
		if (port_data->thread_id == k_current_get())
			return;

		k_wakeup(port_data->thread_id);

		/* Sleep this task if we are not suspended */
		while (tc_is_port_enabled(port_data)) {
			if (++wait > SUSPEND_SLEEP_RETRIES) {
				LOG_WARN("C%d: NOT SUSPENDED after %dms\n",
					port_data->tc.port,
					wait * SUSPEND_SLEEP_DELAY);
				return;
			}
			msleep(SUSPEND_SLEEP_DELAY);
		}
	} else {
		TC_CLR_FLAG(port_data, TC_FLAGS_REQUEST_SUSPEND);
		k_wakeup(port_data->thread_id);
	}
}

void tc_set_error_recovery(struct usbc_port_data *port_data)
{
	TC_SET_FLAG(port_data, TC_FLAGS_REQUEST_ERROR_RECOVERY);
}

int tc_update_cc(struct usbc_port_data *port_data)
{
	int rv;
	enum tc_cc_pull pull = port_data->tc.select_cc_pull;
	enum tc_rp_value rp = TC_RP_USB;

	rv = tcpc_set_cc(port_data->tc.tcpc_dev, pull, rp);
	if (rv)
		LOG_ERR("C%d: TCPC set CC failed (%d)",
			port_data->tc.port, rv);
	return rv;
}

void tc_start_error_recovery(struct usbc_port_data *port_data)
{
	assert(port_data->thread_id == k_current_get());

	/*
	 *   The port should transition to the ErrorRecovery state
	 *   from any other state when directed.
	 */
	set_state_tc(port_data, TC_ERROR_RECOVERY);
}

static void restart_tc_sm(
	struct usbc_port_data *port_data,
	enum usbc_tc_state start_state)
{
	int rv;

	/* Clear flags before we transitions states */
	port_data->tc.flags = 0;

	rv = tcpc_init(port_data->tc.tcpc_dev);

	LOG_PRINTK("C%d: TCPC init %s\n", port_data->tc.port,
		   rv ? "failed" : "ready");

	/* Disable if restart failed, otherwise start in default state. */
	set_state_tc(port_data, rv ? TC_DISABLED : start_state);
}

void tc_state_init(
	const struct usbc_port_config *const port_config,
	struct usbc_port_data *const port_data)
{
	enum usbc_tc_state first_state;

	/*
	 * Determine the initial state to start this port's state machine.
	 */
	first_state = (port_data->tc.sm_initial_state == NULL)
				? TC_ERROR_RECOVERY
				: port_data->tc.sm_initial_state(port_data);

	/*
	 * Initilialize the timers used in this TC state machine
	 *	CC_DEBOUNCE, PD_DEBOUNCE and TIMEOUT
	 */
	k_timer_init(&port_data->tc.timer_cc_debounce, NULL, NULL);
	k_timer_init(&port_data->tc.timer_pd_debounce, NULL, NULL);
	k_timer_init(&port_data->tc.timer_timeout, NULL, NULL);

	/*
	 * Start the state machine.
	 */
	restart_tc_sm(port_data, first_state);
}


/*****************************************************************************
 * Private Functions
 */

/* Get the previous TypeC state. */
static enum usbc_tc_state get_last_state_tc(
	const struct usbc_port_data *port_data)
{
	return port_data->tc.ctx.previous - &tc_states[0];
}

static void print_current_state(
	const struct usbc_port_data *port_data)
{
	if (CONFIG_USBC_TC_SM_LOG_LEVEL != LOG_LEVEL_NONE) {
		if (tc_debug_level > 1) {
			LOG_INF("C%d: %s\n", port_data->tc.port,
				tc_state_names[get_state_tc(port_data)]);
		}
	} else {
		LOG_PRINTK("C%d: tc-st%d\n", port_data->tc.port,
			   get_state_tc(port_data));
	}
}


static void sink_power_sub_states(
	struct usbc_port_data *port_data)
{
	int rv;
	enum tc_cc_voltage_status cc1, cc2, cc;
	enum tc_cc_voltage_status new_cc_voltage;

	rv = tcpc_get_cc(port_data->tc.tcpc_dev, &cc1, &cc2);
	if (rv) {
		LOG_ERR("C%d: TCPC get CC failed (%d)", port_data->tc.port, rv);
		return;
	}

	cc = polarity_rm_dts(port_data->tc.polarity) ? cc2 : cc1;

	switch (cc)
	case TC_CC_VOLT_RP_DEF:
	case TC_CC_VOLT_RP_1_5:
	case TC_CC_VOLT_RP_3_0:
		new_cc_voltage = cc;
		break;
	default:
		new_cc_voltage = TC_CC_VOLT_OPEN;
	}

	/* Debounce the cc state */
	if (new_cc_voltage != port_data->tc.cc_voltage) {
		port_data->tc.cc_voltage = new_cc_voltage;
		k_timer_start(&port_data->tc.timer_cc_debounce,
			      K_MSEC(PD_T_RP_VALUE_CHANGE),
			      K_NO_WAIT);
		return;
	}

	/* If the timer has expired, handle it */
	if (k_timer_status_get(&port_data->tc.timer_cc_debounce))
		k_timer_stop(&port_data->tc.timer_cc_debounce);
}


static void tc_detach(
	struct usbc_port_data *port_data)
{
	int rv;

	if (USBC_TC_INCLUDES_DBGACC_OPTION &&
	    port_data->tc.supports_debug_accessory) {
		rv = tcpc_set_debug_accessory(port_data->tc.tcpc_dev, false);
		if (rv) {
			LOG_ERR("C%d: TCPC set DebugAcc failed (%d)",
				port_data->tc.port, rv);
		}
	}
}

/*
 * TCPC CC/Rp management
 */
static void tc_select_pull(
	struct usbc_port_data *port_data,
	enum tcpc_cc_pull pull)
{
	port_data->tc.select_cc_pull = pull;
}


/*****************************************************************************
 * TYPE-C State Implementations
 */

/**
 * Disabled
 *
 * Super State Entry Actions:
 *   Remove the terminations from CC
 *   Set VBUS and VCONN off
 */
static void tc_snk_disabled_entry(void *port_obj)
{
	struct usbc_port_data *port_data = port_obj;

	print_current_state(port_data);

	/*
	 * We have completed tc_cc_open_entry (our super state), so set flag
	 * to indicate to tc_is_port_enabled that we are now suspended.
	 */
	TC_SET_FLAG(port_data, TC_FLAGS_SUSPENDED);
}

static void tc_snk_disabled_run(void *port_obj)
{
	struct usbc_port_data *port_data = port_obj;

	/* If tc_set_suspend clears the request, go to TC_UNATTACHED_SNK. */
	if (!TC_CHK_FLAG(port_data, TC_FLAGS_REQUEST_SUSPEND)) {
		set_state_tc(port_data, TC_UNATTACHED_SNK);
		return;
	}
	tc_pause_event_loop(port_data);
}

static void tc_snk_disabled_exit(void *port_obj)
{
	int rv;
	struct usbc_port_data *port_data = port_obj;

	tc_start_event_loop(port_data);
	TC_CLR_FLAG(port_data, TC_FLAGS_SUSPENDED);

	rv = tcpc_init(port_data->tc.tcpc_dev);
	LOG_PRINTK("C%d: TCPC init %s\n", port_data->tc.port,
		   rv ? "failed!" : "ready");
}

/**
 * ErrorRecovery
 *
 * Super State Entry Actions:
 *   Remove the terminations from CC
 *   Set's VBUS and VCONN off
 */
static void tc_snk_error_recovery_entry(void *port_obj)
{
	struct usbc_port_data *port_data = port_obj;

	print_current_state(port_data);

	k_timer_start(&port_data->tc.timer_timeout,
		      K_MSEC(PD_T_ERROR_RECOVERY)
		      K_NO_WAIT);

	TC_CLR_FLAG(port_data, TC_FLAGS_REQUEST_ERROR_RECOVERY);
}

static void tc_snk_error_recovery_run(void *port_obj)
{
	struct usbc_port_data *port_data = port_obj;

	if (!k_timer_status_get(&port_data->tc.timer_timeout))
		return;

	/*
	 * If we transitioned to error recovery as the first state and we
	 * didn't brown out, we don't need to reinitialized the tc statemachine
	 * because we just did that. So transition to the state directly.
	 */
	if (port_data->tc.ctx.previous == NULL) {
		set_state_tc(port_data, TC_UNATTACHED_SNK);
		return;
	}
	restart_tc_sm(port_data, TC_UNATTACHED_SNK);
}

static void tc_snk_error_recovery_exit(void *port_obj)
{
	struct usbc_port_data *port_data = port_obj;

	k_timer_stop(&port_data->tc.timer_timeout);
}

/**
 * Unattached.SNK
 */
static void tc_snk_unattached_snk_entry(void *port_obj)
{
	int rv;
	struct usbc_port_data *port_data = port_obj;

	print_current_state(port_data);

	/* Detach from the TC Port */
	tc_detach(port_data);

	/*
	 * We are in an unattached state and considering to be a SNK
	 * searching for a SRC partner.  We set the CC pull value to
	 * to indicate our intent to be SNK in hopes a partner SRC
	 * will is there to attach to.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd.
	 *
	 * Restore default current limit Rp in case we swap to source
	 *
	 * Run any debug detaches needed before setting CC, as some TCPCs may
	 * require we set CC Open before changing power roles with a debug
	 * accessory.
	 */
	if (USBC_TC_INCLUDES_DBGACC_OPTION &&
	    port_data->tc.supports_debug_accessory) {
		rv = tcpc_set_debug_detach(port_data->tc.tcpc_dev);
		if (rv) {
			LOG_ERR("C%d: TCPC set Debug Detach failed (%d)",
				port_data->tc.port, rv);
			return;
		}
	}

	tc_select_pull(port_data, TC_CC_RD);
	tc_update_cc(port_data);

	/* Notify TCPC of role update */
	rv = tcpc_set_roles(port_data->tc.tcpc_dev,
			    TC_ROLE_SINK,
			    TC_ROLE_DISCONNECTED);
	if (rv) {
		LOG_ERR("C%d: TCPC set role failed (%d)",
			port_data->tc.port, rv);
		return;
	}
}

static void tc_snk_unattached_snk_run(void *port_obj)
{
	int rv;
	enum tc_cc_voltage_state cc1, cc2;
	struct usbc_port_data *port_data = port_obj;

	/* Check for connection */
	rv = tcpc_get_cc(port_data->tc.tcpc_dev, &cc1, &cc2);
	if (ret) {
		LOG_ERR("C%d: TCPC get CC failed (%d)",
			port_data->tc.port, rv);
		return;
	}

	/*
	 * The port shall transition to AttachWait.SNK when a Source
	 * connection is detected, as indicated by the SNK.Rp state
	 * on at least one of its CC pins.
	 */
	if (cc_is_rp(cc1) || cc_is_rp(cc2)) {
		/* Connection Detected */
		set_state_tc(port_data, TC_ATTACH_WAIT_SNK);
		return;
	}
}

/**
 * AttachWait.SNK
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rd on CC
 *   Set power role to SINK
 */
static void tc_snk_attach_wait_snk_entry(void *port_obj)
{
	struct usbc_port_data *port_data = port_obj;

	print_current_state(port_data);

	port_data->tc.cc_state = PD_CC_UNSET;
}

static void tc_snk_attach_wait_snk_run(void *port_obj)
{
	int rv;
	enum tc_cc_voltage_status cc1, cc2;
	enum tc_cc_states new_cc_state;
	struct usbc_port_data *port_data = port_obj;

	/* Check for connection */
	rv = tcpc_get_cc(port_data->tc.tcpc_dev, &cc1, &cc2);
	if (rv) {
		LOG_ERR("C%d: TCPC get CC failed (%d)",
			port_data->tc.port, rv);
		return;
	}

	if (cc_is_rp(cc1) && cc_is_rp(cc2) && board_is_dts_port(port_data))
		new_cc_state = PD_CC_DFP_DEBUG_ACC;
	else if (cc_is_rp(cc1) || cc_is_rp(cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else
		new_cc_state = PD_CC_NONE;

	/* Debounce the cc state */
	if (new_cc_state != port_data->tc.cc_state) {
		k_timer_start(&port_data->tc.timer_cc_debounce,
			      K_MSEC(PD_T_CC_DEBOUNCE),
			      K_NO_WAIT);
		k_timer_start(&port_data->tc.timer_pd_debounce,
			      K_MSEC(PD_T_PD_DEBOUNCE),
			      K_NO_WAIT);
		port_data->tc.cc_state = new_cc_state;
		return;
	}

	/*
	 * Transition to Unattached.SNK when the state of both the CC1 and
	 * CC2 pins is SNK.Open for at least tPDDebounce.
	 */
	if (new_cc_state == PD_CC_NONE &&
	    k_timer_status_get(&port_data->tc.timer_pd_debounce)) {
		/* We are detached */
		set_state_tc(port_data, TC_UNATTACHED_SNK);
		return;
	}

	/* Wait for CC debounce */
	if (!k_timer_status_get(&port_data->tc.timer_cc_debounce))
		return;

	/*
	 * The port shall transition to Attached.SNK after the state of only
	 * one of the CC1 or CC2 pins is SNK.Rp for at least tCCDebounce and
	 * VBUS is detected.
	 *
	 * If the port supports Debug Accessory Mode, the port shall transition
	 * to DebugAccessory.SNK if the state of both the CC1 and CC2 pins is
	 * SNK.Rp for at least tCCDebounce and VBUS is detected.
	 */
	if (pd_is_vbus_present(port_data)) {
		if (USBC_TC_INCLUDES_DBGACC_OPTION &&
		    port_data->tc.supports_debug_accessory)
			set_state_tc(port_data,
				(new_cc_state == PD_CC_DFP_ATTACHED)
					? TC_ATTACHED_SNK
					: TC_DEBUG_ACCESSORY_SNK);
		else
			set_state_tc(port_data, TC_ATTACHED_SNK);
		return;
	}
}

static void tc_snk_attach_wait_snk_exit(void *port_obj)
{
	struct usbc_port_data *port_data = port_obj;

	k_timer_stop(&port_data->tc.timer_cc_debounce);
	k_timer_stop(&port_data->tc.timer_pd_debounce);
}

/**
 * Attached.SNK, shared with Debug Accessory.SNK
 */
static void tc_snk_attached_debug_accessory_snk_shared_entry(void *port_obj)
{
	int rv;
	enum tc_cc_voltage_state cc1, cc2;
	struct usbc_port_data *port_data = port_obj;

	/*
	 * Known state of attach is SNK.  We need to apply this pull value
	 * to make it set in hardware at the correct time but set the common
	 * pull here.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd.
	 */
	tc_select_pull(port_data, TC_CC_RD);

	/* Get connector orientation */
	rv = tcpc_get_cc(port_data->tc.tcpc_dev, &cc1, &cc2);
	if (rv) {
		LOG_ERR("C%d: TCPC get CC failed (%d)",
			port_data->tc.port, rv);
		return;
	}

	port_data->tc.polarity = get_snk_polarity(cc1, cc2);
	tcpm_set_polarity(port_data->tc.tcpm, port_data->tc.polarity);

	/* Notify TCPC of role update */
	rv = tcpc_set_roles(port_data->tc.tcpc_dev,
			    TC_ROLE_SINK,
			    TC_ROLE_UFP);
	if (rv) {
		LOG_ERR("C%d: TCPC set role failed (%d)",
			port_data->tc.port, rv);
		return;
	}

	/* Apply Rd */
	tc_update_cc(port_data);

	/*
	 * Attached.SNK - enable AutoDischargeDisconnect
	 * Do this after applying Rd to CC lines to avoid
	 * TCPC_REG_FAULT_STATUS_AUTO_DISCHARGE_FAIL
	 */
	rv = tcpc_enable_auto_discharge_disconnect(port_data->tc.tcpc_dev,
						   true);
	if (rv) {
		LOG_ERR("C%d: TCPC "
			"enable AutoDischargeDisconnect failed (%d)",
			port_data->tc.port, rv);
		return;
	}

	k_timer_stop(&port_data->tc.timer_cc_debounce);
}
static void tc_snk_attached_snk_entry(void *port_obj)
{
	struct usbc_port_data *port_data = port_obj;

	print_current_state(port_data);

	tc_snk_attached_debug_accessory_snk_shared_entry(port_data);
}
#if (USBC_TC_INCLUDES_DBGACC_OPTION)
static void tc_snk_debug_accessory_snk_entry(void *port_obj)
{
	int rv;
	struct usbc_port_data *port_data = port_obj;

	print_current_state(port_data);

	tc_snk_attached_debug_accessory_snk_shared_entry(port_data);

	rv = tcpc_set_debug_accessory(port_data->tc.tcpc_dev, true);
	if (rv) {
		LOG_ERR("C%d: TCPC set DebugAcc failed (%d)",
			port_data->tc.port, rv);
		return;
	}
}
#endif /* USBC_TC_INCLUDES_DBGACC_OPTION */


static void tc_snk_attached_debug_accessory_snk_shared_run(void *port_obj)
{
	struct usbc_port_data *port_data = port_obj;

	/* Detach detection */
	if (tcpc_check_vbus_level(port_data, VBUS_REMOVED)) {
		set_state_tc(port_data, TC_UNATTACHED_SNK);
		return;
	}

	/* Run Sink Power Sub-State */
	sink_power_sub_states(port_data);
}
static void tc_snk_attached_snk_run(void *port_obj)
{
	tc_snk_attached_debug_accessory_snk_shared_run(port_obj);
}
#if (USBC_TC_INCLUDES_DBGACC_OPTION)
static void tc_snk_debug_accessory_snk_run(void *port_obj)
{
	tc_snk_attached_debug_accessory_snk_shared_run(port_obj);
}
#endif /* USBC_TC_INCLUDES_DBGACC_OPTION */


static void tc_snk_attached_debug_accessory_snk_shared_exit(void *port_obj)
{
	int rv;
	struct usbc_port_data *port_data = port_obj;

	/*
	 * Attached.SNK exit - disable AutoDischargeDisconnect
	 * NOTE: This should not happen if we are suspending. It will
	 * happen in tc_cc_open_entry if that is the path we are
	 * taking.
	 */
	if (!TC_CHK_FLAG(port_data, TC_FLAGS_REQUEST_SUSPEND)) {
		rv = tcpc_enable_auto_discharge_disconnect(
				port_data->tc.tcpc_dev,
				false);
		if (rv) {
			LOG_ERR("C%d: TCPC "
				"enable AutoDischargeDisconnect failed (%d)",
				port_data->tc.port, rv);
			return;
		}
	}

	k_timer_stop(&port_data->tc.timer_cc_debounce);
	k_timer_stop(&port_data->tc.timer_timeout);
}
static void tc_snk_attached_snk_exit(void *port_obj)
{
	tc_snk_attached_debug_accessory_snk_shared_exit(port_obj);
}
#if (USBC_TC_INCLUDES_DBGACC_OPTION)
static void tc_snk_debug_accessory_snk_exit(void *port_obj)
{
	int rv;
	struct usbc_port_data *port_data = port_obj;

	tc_snk_attached_debug_accessory_snk_shared_exit(port_data);

	rv = tcpc_set_debug_detach(port_data->tc.tcpc_dev);
	if (rv) {
		LOG_ERR("C%d: TCPC set Debug Detach failed (%d)",
			port_data->tc.port, rv);
		return;
	}
}
#endif /* USBC_TC_INCLUDES_DBGACC_OPTION */


/**
 * Super State CC_OPEN
 */
static void tc_snk_cc_open_entry(void *port_obj)
{
	int rv;
	struct usbc_port_data *port_data = port_obj;

	/*
	 * Ensure we disable discharging before setting CC lines to open.
	 * If we were sourcing above, then we already drained Vbus. If partner
	 * is sourcing Vbus they will drain Vbus if they are PD-capable. This
	 * should only be done if a battery is present as a batteryless
	 * device will brown out when AutoDischargeDisconnect is disabled and
	 * we do not want this to happen until the set_cc open/open to make
	 * sure the TCPC has managed its internal states for disconnecting
	 * the only source of power it has.
	 */
	if (battery_is_present()) {
		rv = tcpc_enable_auto_discharge_disconnect(
				port_data->tc.tcpc_dev,
				false);
		if (rv) {
			LOG_ERR("C%d: TCPC "
				"enable AutoDischargeDisconnect failed (%d)",
				port_data->tc.port, rv);
			return;
		}
	}

	/*
	 * We may brown out after applying CC open, so flush console first.
	 * Console flush can take a long time, so if we aren't in danger of
	 * browning out, don't do it so we can meet certain compliance timing
	 * requirements.
	 */
	LOG_PRINTK("C%d: Applying CC Open!\n", port_data->tc.port);
	if (!battery_is_present())
		cflush();

	/* Remove terminations from CC */
	tc_select_pull(port_data, TC_CC_OPEN);
	tc_update_cc(port_data);

	/* Detach from the TC Port */
	tc_detach(port_data);
}

void tc_run(
	const struct usbc_port_config *const port_config,
	struct usbc_port_data *const port_data)
{
	/*
	 * If tc_set_suspend set TC_FLAGS_REQUEST_SUSPEND, go directly to
	 * TC_DISABLED.
	 */
	if (get_state_tc(port_data) != TC_DISABLED
	    && TC_CHK_FLAG(port_data, TC_FLAGS_REQUEST_SUSPEND))
		set_state_tc(port_data, TC_DISABLED);

	/* If error recovery has been requested, transition now */
	if (TC_CHK_FLAG(port_data, TC_FLAGS_REQUEST_ERROR_RECOVERY))
		set_state_tc(port_data, TC_ERROR_RECOVERY);

	run_state(port_data, &port_data->tc.ctx);
}

/*
 * Type-C State Hierarchy (Sub-States are listed inside the boxes)
 *
 * |TC_CC_OPEN -----------|
 * |                      |
 * |	TC_DISABLED       |
 * |	TC_ERROR_RECOVERY |
 * |----------------------|
 *
 * TC_UNATTACHED_SNK   TC_ATTACH_WAIT_SNK   TC_ATTACHED_SNK
 * TC_DEBUG_ACCESSORY_SNK
 *
 */
static __const_data const struct usbc_state tc_states[] = {
	/* Super States */
	[TC_CC_OPEN] = {
		.entry	= tc_snk_cc_open_entry,
	},
	/* Normal States */
	[TC_DISABLED] = {
		.entry	= tc_snk_disabled_entry,
		.run	= tc_snk_disabled_run,
		.exit	= tc_snk_disabled_exit,
		.parent = &tc_states[TC_CC_OPEN],
	},
	[TC_ERROR_RECOVERY] = {
		.entry	= tc_snk_error_recovery_entry,
		.run	= tc_snk_error_recovery_run,
		.exit   = tc_snk_error_recovery_exit,
		.parent = &tc_states[TC_CC_OPEN],
	},
	[TC_UNATTACHED_SNK] = {
		.entry	= tc_snk_unattached_snk_entry,
		.run	= tc_snk_unattached_snk_run,
	},
	[TC_ATTACH_WAIT_SNK] = {
		.entry	= tc_snk_attach_wait_snk_entry,
		.run	= tc_snk_attach_wait_snk_run,
		.exit	= tc_snk_attach_wait_snk_exit,
	},
	[TC_ATTACHED_SNK] = {
		.entry	= tc_snk_attached_snk_entry,
		.run	= tc_snk_attached_snk_run,
		.exit	= tc_snk_attached_snk_exit,
	},
#if (USBC_TC_INCLUDES_DBGACC_OPTION)
	[TC_DEBUG_ACCESSORY_SNK] = {
		.entry	= tc_snk_debug_accessory_snk_entry,
		.run	= tc_snk_debug_accessory_snk_run,
		.exit	= tc_snk_debug_accessory_snk_exit,
	},
#endif /* USBC_TC_INCLUDES_DBGACC_OPTION */
};
