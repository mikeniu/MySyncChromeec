/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "board.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_prl_sm.h"
#include "tcpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"
#include "version.h"
#include "vpd_api.h"

#undef PD_DEFAULT_STATE
/* Port default state at startup */
#define PD_DEFAULT_STATE(port) tc_state_unattached_snk

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_HOOK, format, ## args)
#define CPRINTS(format, args...) cprints(CC_HOOK, format, ## args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

#define TC_OBJ(port)   (SM_OBJ(tc[port]))

static struct type_c {
	/* struct sm_obj must be first */
	struct sm_obj obj;
	/* current port power role (SOURCE or SINK) */
	uint8_t power_role;
	/* current port data role (DFP or UFP) */
	uint8_t data_role;
	/* enable power delivery state machines */
	uint8_t pd_enable;
	/* event timeout */
	uint64_t evt_timeout;
	/* state machine event */
	int evt;
	/* Time a port shall wait before it can determine it is attached */
	uint64_t cc_debounce;
	/* Time a Sink port shall wait before it can determine it is detached
	 * due to the potential for USB PD signaling on CC as described in
	 * the state definitions.
	 */
	uint64_t pd_debounce;
	/*
	 * Time a port shall wait before it can determine it is
	 * re-attached during the try-wait process.
	 */
	uint64_t try_wait_debounce;
#ifdef CONFIG_USB_TYPEC_VPD_CT
	/* charge-through support timer */
	uint64_t support_timer;
	/* reset the charge-through support timer */
	int support_timer_reset;
#endif
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
	/* VPD host port cc state */
	enum pd_cc_states host_cc_state;
	enum pd_cc_states host_new_cc_state;
	uint8_t ct_cc;
#endif
	/* The cc state */
	enum pd_cc_states cc_state;
	enum pd_cc_states new_cc_state;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	uint64_t next_role_swap;
#endif
} tc[CONFIG_USB_PD_PORT_COUNT];

/* Type-C states */
static unsigned int tc_state_disabled(int port, int sig);

static unsigned int tc_state_unattached_snk(int port, int sig);
static unsigned int tc_state_attach_wait_snk(int port, int sig);
static unsigned int tc_state_attached_snk(int port, int sig);

#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
/* Super States */
static unsigned int tc_state_host_rd_ct_rd(int port, int sig);
static unsigned int tc_state_host_open_ct_open(int port, int sig);
static unsigned int tc_state_vbus_cc_iso(int port, int sig);
#endif

#ifdef CONFIG_USB_TYPEC_VPD_CT
static unsigned int tc_state_error_recovery(int port, int sig);

static unsigned int tc_state_try_snk(int port, int sig);
static unsigned int tc_state_unattached_src(int port, int sig);
static unsigned int tc_state_attach_wait_src(int port, int sig);
static unsigned int tc_state_try_wait_src(int port, int sig);
static unsigned int tc_state_attached_src(int port, int sig);

/* Charge-Through States */
static unsigned int tc_state_ct_try_snk(int port, int sig);
static unsigned int tc_state_ct_attach_wait_unsupported(int port, int sig);
static unsigned int tc_state_ct_attached_unsupported(int port, int sig);
static unsigned int tc_state_ct_unattached_unsupported(int port, int sig);
static unsigned int tc_state_ct_unattached_vpd(int port, int sig);
static unsigned int tc_state_ct_disabled_vpd(int port, int sig);
static unsigned int tc_state_ct_attached_vpd(int port, int sig);
static unsigned int tc_state_ct_attach_wait_vpd(int port, int sig);

/* Super States */
static unsigned int tc_state_host_rp3_ct_rd(int port, int sig);
static unsigned int tc_state_host_rp3_ct_rpu(int port, int sig);
static unsigned int tc_state_host_rpu_ct_rd(int port, int sig);

void tc_reset_support_timer(int port)
{
	tc[port].support_timer_reset++;
}
#endif /* CONFIG_USB_TYPEC_VPD_CT */

int tc_get_power_role(int port)
{
	return tc[port].power_role;
}

int tc_get_data_role(int port)
{
	return tc[port].data_role;
}

void tc_set_timeout(int port, uint64_t timeout)
{
	tc[port].evt_timeout = timeout;
}

/**
 * Returns whether the sink has detected a Rp resistor on the other side.
 */
static inline int cc_is_rp(int cc)
{
	return (cc == TYPEC_CC_VOLT_SNK_DEF) || (cc == TYPEC_CC_VOLT_SNK_1_5) ||
	       (cc == TYPEC_CC_VOLT_SNK_3_0);
}

/*
 * CC values for regular sources and Debug sources (aka DTS)
 *
 * Source type  Mode of Operation   CC1    CC2
 * ---------------------------------------------
 * Regular      Default USB Power   RpUSB  Open
 * Regular      USB-C @ 1.5 A       Rp1A5  Open
 * Regular      USB-C @ 3 A         Rp3A0  Open
 * DTS          Default USB Power   Rp3A0  Rp1A5
 * DTS          USB-C @ 1.5 A       Rp1A5  RpUSB
 * DTS          USB-C @ 3 A         Rp3A0  RpUSB
 */

#ifdef CONFIG_COMMON_RUNTIME
/* Initialize globals based on system state. */
static void pd_init_tasks(void)
{
	static int initialized;

#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
	tc[0].power_role = PD_ROLE_VPD;
	tc[0].data_role = 0; /* Reserved for VPD */
#endif
	/* Initialize globals once, for all PD tasks.  */
	if (initialized)
		return;

	initialized = 1;
}
#endif /* CONFIG_COMMON_RUNTIME */

static int pd_restart_tcpc(int port)
{
	tcpm_init(port);
	return 0;
}

void pd_task(void *u)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());
	int res = 0;
	sm_state this_state;

	pd_init_tasks();

	pd_restart_tcpc(port);

	CPRINTS("TCPC p%d init %s", port, res ? "failed" : "ready");
	this_state = res ? tc_state_disabled : PD_DEFAULT_STATE(port);

	/* Disable TCPC RX until connection is established */
	tcpm_set_rx_enable(port, 0);

	/* Disable pd state machines */
	tc[port].pd_enable = 0;

	init_state(port, TC_OBJ(port), this_state);

	tc[port].evt_timeout = 10*MSEC;

	while (1) {
		/* wait for next event/packet or timeout expiration */
		tc[port].evt = task_wait_event(tc[port].evt_timeout);

#ifdef CONFIG_USB_PD_TCPC
		/*
		 * run port controller task to check CC and/or read incoming
		 * messages
		 */
		tcpc_run(port, tc[port].evt);
#endif

#ifdef CONFIG_USB_PE_SM
		/* run policy engine state machine */
		policy_engine(port, tc[port].evt, tc[port].pd_enable);
#endif /* CONFIG_USB_PE_SM */

#ifdef CONFIG_USB_PRL_SM
		/* run protocol state machine */
		protocol_layer(port, tc[port].evt, tc[port].pd_enable);
#endif /* CONFIG_USB_PRL_SM */

		/* run state machine */
		exe_state(port, TC_OBJ(port), RUN_SIG);
	}
}

static unsigned int tc_state_disabled(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: Disabled", port);
		break;
	case RUN_SIG:
		task_wait_event(-1);

	RUN_SIG_DONE;
	case EXIT_SIG:
#ifndef CONFIG_USB_PD_TCPC
		if (pd_restart_tcpc(port) != 0) {
			CPRINTS("TCPC p%d restart failed!", port);
			break;
		}
#endif
		CPRINTS("TCPC p%d resumed!", port);
		SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_open_ct_open);
}

#ifdef CONFIG_USB_TYPEC_VPD_CT
static unsigned int tc_state_error_recovery(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Use cc_debounce state variable for error recovery timeout */
		tc[port].cc_debounce = get_time().val + PD_T_ERROR_RECOVERY;
		break;
	case RUN_SIG:
		if (get_time().val > tc[port].cc_debounce)
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_open_ct_open);
}
#endif /* CONFIG_USB_TYPEC_VPD_CT */

static unsigned int tc_state_unattached_snk(int port, int sig)
{
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
#ifdef CONFIG_USB_TYPEC_VPD_CT
	int cc1;
	int cc2;
#endif
	int host_cc;
#endif /* CONFIG_USB_TYPEC_VPD || CONFIG_USB_TYPEC_VPD_CT */

	switch (sig) {
	case ENTRY_SIG:
#ifdef CONFIG_USB_TYPEC_VPD_CT
		if (tc[port].obj.last_state != tc_state_unattached_src)
#endif
			CPRINTS("C%d: Unattached.SNK", port);
#ifdef CONFIG_USB_TYPEC_VPD_CT
		tc[port].host_cc_state = -1;
		tc[port].host_new_cc_state = -1;
		tc[port].cc_state = -1;
		tc[port].new_cc_state = -1;
#endif /* CONFIG_USB_TYPEC_VPD_CT */
		break;
	case RUN_SIG:
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
		/* Check Host CC for connection */
		vpd_host_get_cc(&host_cc);

#if defined(CONFIG_USB_TYPEC_VPD) && !defined(CONFIG_USB_TYPEC_VPD_CT)
		/* Check host CC for connection */
		if (cc_is_rp(host_cc))
			SET_STATE(port, TC_OBJ(port), tc_state_attach_wait_snk);
#else
		/* Check host CC for connection */
		if (cc_is_rp(host_cc))
			tc[port].host_new_cc_state = PD_CC_DFP_ATTACHED;
		else
			tc[port].host_new_cc_state = PD_CC_NONE;

		/*
		 * Transition to AttachWait.SNK when a Source connection is
		 * detected, as indicated by the SNK.Rp state on its Host-side
		 * port’s CC pin.
		 */
		if (tc[port].host_new_cc_state == PD_CC_DFP_ATTACHED)
			SET_STATE(port, TC_OBJ(port), tc_state_attach_wait_snk);
#endif /* CONFIG_USB_TYPEC_VPD && not CONFIG_USB_TYPEC_VPD_CT */

#ifdef CONFIG_USB_TYPEC_VPD_CT
		/* Debounce Host CC state */
		if (tc[port].host_new_cc_state != tc[port].host_cc_state) {
			tc[port].host_cc_state = tc[port].host_new_cc_state;
			tc[port].next_role_swap = get_time().val + PD_T_DRP_SRC;
			STATE_RETURN;
		}

		/* Wait for Host CC debounce */
		if (get_time().val < tc[port].next_role_swap)
			STATE_RETURN;

		/*
		 * If we are here, Host cc is open and debounced
		 */

		/* Check Charge-Through CCs for connection */
		vpd_ct_get_cc(&cc1, &cc2);

		/*
		 * Transition to Unattached.SRC when SNK.Rp state is detected on
		 * exactly one of the the Charge-Through CC1 or CC2 pins for
		 * at least tCCDebounce and VBUS is detected
		 */
		if ((cc_is_rp(cc1) && !cc_is_rp(cc2)) ||
					(!cc_is_rp(cc1) && cc_is_rp(cc2)))
			tc[port].new_cc_state = PD_CC_DFP_ATTACHED;
		else
			tc[port].new_cc_state = PD_CC_NONE;

		/* Debounce Charge-Through CC state */
		if (tc[port].cc_state != tc[port].new_cc_state) {
			tc[port].cc_state = tc[port].new_cc_state;
			tc[port].cc_debounce =
					get_time().val + PD_T_CC_DEBOUNCE;
			STATE_RETURN;
		}

		/* Wait for CC debounce */
		if (get_time().val < tc[port].cc_debounce)
			STATE_RETURN;

		if (vpd_is_ct_vbus() &&
				tc[port].cc_state == PD_CC_DFP_ATTACHED)
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_src);
#endif /* CONFIG_USB_TYPEC_VPD_CT */
#endif /* CONFIG_USB_TYPEC_VPD || CONFIG_USB_TYPEC_VPD_CT */
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
	/* return super state */
	return SUPER(sig, tc_state_host_rd_ct_rd);
#else /* CONFIG_USB_TYPEC_VPD || CONFIG_USB_TYPEC_VPD_CT */
	return 0;
#endif
}

static unsigned int tc_state_attach_wait_snk(int port, int sig)
{
	int host_cc;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: AttachWait.SNK", port);
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
		tc[port].host_cc_state = -1;
		tc[port].host_new_cc_state = -1;
#endif /* CONFIG_USB_TYPEC_VPD || CONFIG_USB_TYPEC_VPD_CT */
		break;
	case RUN_SIG:
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
		/* Check Host CC for connection */
		vpd_host_get_cc(&host_cc);

		if (cc_is_rp(host_cc))
			tc[port].host_new_cc_state = PD_CC_DFP_ATTACHED;
		else
			tc[port].host_new_cc_state = PD_CC_NONE;

		/* Debounce the Host CC state */
		if (tc[port].host_cc_state != tc[port].host_new_cc_state) {
			tc[port].host_cc_state = tc[port].host_new_cc_state;
			tc[port].cc_debounce =
					get_time().val + PD_T_CC_DEBOUNCE;
			STATE_RETURN;
		}

		/* Wait for Host CC debounce */
		if (get_time().val < tc[port].cc_debounce)
			STATE_RETURN;

		if (tc[port].host_cc_state == PD_CC_DFP_ATTACHED &&
				(vpd_is_vconn() || vpd_is_host_vbus())) {
			SET_STATE(port, TC_OBJ(port), tc_state_attached_snk);
		} else {
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);
		}
#endif /* CONFIG_USB_TYPEC_VPD || CONFIG_USB_TYPEC_VPD_CT */
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
	/* return super state */
	return SUPER(sig, tc_state_host_rd_ct_rd);
#else /* CONFIG_USB_TYPEC_VPD || CONFIG_USB_TYPEC_VPD_CT */
	return 0;
#endif
}

static unsigned int tc_state_attached_snk(int port, int sig)
{
#ifdef CONFIG_USB_TYPEC_VPD_CT
	int host_cc;
#endif
	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: Attached.SNK", port);

		/* Enable PD */
		tc[port].pd_enable = 1;
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
		/* Enable RX */
		vpd_rx_enable(1);

#ifdef CONFIG_USB_TYPEC_VPD_CT
		/*
		 * This state can only be entered from states AttachWait.SNK
		 * and Try.SNK. So the Host port is isolated from the
		 * Charge-Through port. We only need to High-Z the
		 * Charge-Through ports CC1 and CC2 pins.
		 */
		vpd_ct_set_pull(TYPEC_CC_OPEN, 0);

		tc[port].host_cc_state = -1;
		tc[port].host_new_cc_state = -1;

		/* Start Charge-Through support timer */
		tc[port].support_timer = get_time().val + PD_T_AME;
		tc[port].support_timer_reset = 0;
#endif /* CONFIG_USB_TYPEC_VPD_CT */
#endif /* CONFIG_USB_TYPEC_VPD || CONFIG_USB_TYPEC_VPD_CT */
		break;
	case RUN_SIG:
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
#ifdef CONFIG_USB_TYPEC_VPD_CT
		/*
		 * Reset the Charge-Through Support Timer when it first
		 * receives any USB PD Structured VDM Command it supports,
		 * which is the Discover Identity command. And this is only
		 * done one time.
		 */
		if (tc[port].support_timer_reset == 1) {
			tc[port].support_timer_reset++;
			tc[port].support_timer = get_time().val + PD_T_AME;
		}
#endif /* CONFIG_USB_TYPEC_VPD_CT */

		/* Has host vbus been and vconn been removed */
		if (!vpd_is_host_vbus() && !vpd_is_vconn())
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);

#ifdef CONFIG_USB_TYPEC_VPD_CT
		/* Check Host CC for connection */
		vpd_host_get_cc(&host_cc);

		if (cc_is_rp(host_cc))
			tc[port].host_new_cc_state = PD_CC_DFP_ATTACHED;
		else
			tc[port].host_new_cc_state = PD_CC_NONE;

		/* Debounce the Host CC state */
		if (tc[port].host_cc_state != tc[port].host_new_cc_state) {
			tc[port].host_cc_state = tc[port].host_new_cc_state;
			tc[port].cc_debounce =
					get_time().val + PD_T_VPDCTDD;
			STATE_RETURN;
		}

		/* Wait for Host CC debounce */
		if (get_time().val < tc[port].cc_debounce)
			STATE_RETURN;

		if (tc[port].host_cc_state == PD_CC_NONE && vpd_is_vconn())
			SET_STATE(port, TC_OBJ(port),
				tc_state_ct_unattached_vpd);

		/* Check the Support Timer */
		if (get_time().val > tc[port].support_timer) {
			/*
			 * Present USB Billboard Device Class interface
			 * indicating that Charge-Through is not supported
			 */
			vpd_present_billboard(1);
			STATE_RETURN;
		}
#endif /* CONFIG_USB_TYPEC_VPD_CT */
#endif /* CONFIG_USB_TYPEC_VPD || CONFIG_USB_TYPEC_VPD_CT */
	RUN_SIG_DONE;
	case EXIT_SIG:
		/* Disable PD */
		tc[port].pd_enable = 0;
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
		/* Disable RX */
		vpd_rx_enable(0);
#endif /* CONFIG_USB_TYPEC_VPD || CONFIG_USB_TYPEC_VPD_CT */
		break;
	}

	/* no super state */
	return 0;
}

#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
static unsigned int tc_state_host_rd_ct_rd(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Place Rd on Host CC */
		vpd_host_set_pull(TYPEC_CC_RD, 0);
#ifdef CONFIG_USB_TYPEC_VPD_CT
		/* Place Rd on Charge-Through CCs */
		vpd_ct_set_pull(TYPEC_CC_RD, 0);
#endif
	break;
	case RUN_SIG:

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_vbus_cc_iso);
}

static unsigned int tc_state_host_open_ct_open(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Remove the terminations from Host */
		vpd_host_set_pull(TYPEC_CC_OPEN, 0);
#ifdef CONFIG_USB_TYPEC_VPD_CT
		/* Remove the terminations from Charge-Through */
		vpd_ct_set_pull(TYPEC_CC_OPEN, 0);
#endif
	break;
	case RUN_SIG:

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_vbus_cc_iso);
}

static unsigned int tc_state_vbus_cc_iso(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
#ifdef CONFIG_USB_TYPEC_VPD_CT
		/* Isolate the Host-side port from the Charge-Through port */
		vpd_vbus_pass_en(0);
		vpd_ct_cc_sel(CT_OPEN);
#endif
		/* Allow control of Host CC Rd with cc_RP3A0_RD_L */
		vpd_cc_db_en_od(GPO_LOW);

		/* Enable mcu communication and cc */
		vpd_mcu_cc_en(1);
		break;
	case RUN_SIG:

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* no super state */
	return 0;
}
#endif

#ifdef CONFIG_USB_TYPEC_VPD_CT
static unsigned int tc_state_unattached_src(int port, int sig)
{
	int host_cc;

	switch (sig) {
	case ENTRY_SIG:
		if (tc[port].obj.last_state != tc_state_unattached_snk)
			CPRINTS("C%d: Unattached.SRC", port);

		/* Get power from VBUS */
		vpd_vconn_pwr_sel_odl(PWR_VBUS);

		/* Make sure it's the Charge-Through Port's VBUS */
		if (!vpd_is_ct_vbus())
			SET_STATE(port, TC_OBJ(port), tc_state_error_recovery);

		tc[port].next_role_swap = get_time().val + PD_T_DRP_SRC;
		break;
	case RUN_SIG:
		/* Check Host CC for connection */
		vpd_host_get_cc(&host_cc);

		/*
		 * Transition to AttachWait.SRC when host-side VBUS is
		 * vSafe0V and SRC.Rd state is detected on the Host-side
		 * port’s CC pin.
		 */
		if (!vpd_is_host_vbus() && host_cc == TYPEC_CC_VOLT_RD)
			SET_STATE(port, TC_OBJ(port), tc_state_attach_wait_src);

		/*
		 * Transition to Unattached.SNK within tDRPTransition or
		 * if Charge-Through VBUS is removed.
		 */
		if (!vpd_is_ct_vbus() ||
				get_time().val > tc[port].next_role_swap)
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_rpu_ct_rd);
}

static unsigned int tc_state_attach_wait_src(int port, int sig)
{
	int host_cc;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: AttachWait.SRC", port);

		tc[port].host_cc_state = -1;
		tc[port].host_new_cc_state = -1;
		break;
	case RUN_SIG:
		/* Check Host CC for connection */
		vpd_host_get_cc(&host_cc);

		if (host_cc == TYPEC_CC_VOLT_RD)
			tc[port].host_new_cc_state = PD_CC_UFP_ATTACHED;
		else
			tc[port].host_new_cc_state = PD_CC_NONE;

		if (tc[port].host_new_cc_state == PD_CC_NONE ||
							!vpd_is_ct_vbus())
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);

		/* Debounce the Host CC state */
		if (tc[port].host_cc_state != tc[port].host_new_cc_state) {
			tc[port].host_cc_state = tc[port].host_new_cc_state;
			tc[port].cc_debounce = get_time().val +
							PD_T_CC_DEBOUNCE;
			STATE_RETURN;
		}

		/* Wait for Host CC debounce */
		if (get_time().val < tc[port].cc_debounce)
			STATE_RETURN;

		/* Debounce complete */
		if (tc[port].host_cc_state == PD_CC_UFP_ATTACHED &&
							!vpd_is_host_vbus())
			SET_STATE(port, TC_OBJ(port), tc_state_try_snk);

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_rpu_ct_rd);
}

static unsigned int tc_state_attached_src(int port, int sig)
{
	int host_cc;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: Attached.SRC", port);

		/* Enable PD */
		tc[port].pd_enable = 1;

		/* Enable RX */
		vpd_rx_enable(1);

		/* Connect Charge-Through VBUS to Host VBUS */
		vpd_vbus_pass_en(1);

		/*
		 * Get power from VBUS. No need to test because
		 * the Host VBUS is connected to the Charge-Through
		 * VBUS
		 */
		vpd_vconn_pwr_sel_odl(PWR_VBUS);

		break;
	case RUN_SIG:
		/* Check Host CC for connection */
		vpd_host_get_cc(&host_cc);
		if (!vpd_is_ct_vbus() || host_cc == TYPEC_CC_VOLT_OPEN)
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);
	RUN_SIG_DONE;
	case EXIT_SIG:
		/* Disable PD */
		tc[port].pd_enable = 0;
		/* Disable RX */
		vpd_rx_enable(0);
		break;
	}

	/* no super state */
	return 0;
}

static unsigned int tc_state_host_rpu_ct_rd(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Place RpUSB on Host CC */
		vpd_host_set_pull(TYPEC_CC_RP, TYPEC_RP_USB);

		/* Place Rd on Charge-Through CCs */
		vpd_ct_set_pull(TYPEC_CC_RD, 0);
		break;
	case RUN_SIG:

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_vbus_cc_iso);
}

static unsigned int tc_state_try_snk(int port, int sig)
{
	int host_cc;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: Try.SNK", port);

		/* Get power from VBUS */
		vpd_vconn_pwr_sel_odl(PWR_VBUS);

		/* Make sure it's the Charge-Through Port's VBUS */
		if (!vpd_is_ct_vbus())
			SET_STATE(port, TC_OBJ(port), tc_state_error_recovery);

		tc[port].host_cc_state = -1;
		tc[port].host_new_cc_state = -1;

		/* Using next_role_swap timer as try_src timer */
		tc[port].next_role_swap = get_time().val + PD_T_TRY_SRC;
		break;
	case RUN_SIG:
		/*
		 * Wait for tDRPTry before monitoring the Charge-Through
		 * port’s CC pins for the SNK.Rp
		 */
		if (get_time().val < tc[port].next_role_swap)
			STATE_RETURN;

		/* Check Host CC for connection */
		vpd_host_get_cc(&host_cc);

		if (cc_is_rp(host_cc))
			tc[port].host_new_cc_state = PD_CC_DFP_ATTACHED;
		else
			tc[port].host_new_cc_state = PD_CC_NONE;

		/* Debounce the Host CC state */
		if (tc[port].host_cc_state != tc[port].host_new_cc_state) {
			tc[port].host_cc_state = tc[port].host_new_cc_state;
			tc[port].cc_debounce = get_time().val + PD_T_DEBOUNCE;
			STATE_RETURN;
		}

		/* Wait for Host CC debounce */
		if (get_time().val < tc[port].cc_debounce)
			STATE_RETURN;

		/* Debounce complete */
		if (tc[port].host_cc_state == PD_CC_DFP_ATTACHED &&
				(vpd_is_host_vbus() || vpd_is_vconn())) {
			SET_STATE(port, TC_OBJ(port), tc_state_attached_snk);
		} else {
			SET_STATE(port, TC_OBJ(port), tc_state_try_wait_src);
		}

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_rd_ct_rd);
}

static unsigned int tc_state_try_wait_src(int port, int sig)
{
	int host_cc;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: TryWait.SRC", port);

		tc[port].host_cc_state = -1;
		tc[port].host_new_cc_state = -1;
		break;
	case RUN_SIG:
		/* Check Host CC for connection */
		vpd_host_get_cc(&host_cc);

		if (host_cc == TYPEC_CC_VOLT_RD)
			tc[port].host_new_cc_state = PD_CC_UFP_ATTACHED;
		else
			tc[port].host_new_cc_state = PD_CC_NONE;

		/* Debounce the Host CC state */
		if (tc[port].host_cc_state != tc[port].host_new_cc_state) {
			tc[port].host_cc_state = tc[port].host_new_cc_state;
			tc[port].cc_debounce = get_time().val + PD_T_DEBOUNCE;
			tc[port].next_role_swap = get_time().val + PD_T_TRY_SRC;
			STATE_RETURN;
		}

		if (get_time().val > tc[port].cc_debounce) {
			/* Debounce complete */
			if (tc[port].host_cc_state == PD_CC_UFP_ATTACHED &&
							!vpd_is_host_vbus())
				SET_STATE(port, TC_OBJ(port),
							tc_state_attached_src);
		}

		if (get_time().val > tc[port].next_role_swap) {
			/* Debounce complete */
			if (tc[port].host_cc_state == PD_CC_NONE)
				SET_STATE(port, TC_OBJ(port),
						tc_state_unattached_snk);
		}

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_rpu_ct_rd);
}

static unsigned int tc_state_ct_try_snk(int port, int sig)
{
	int cc1;
	int cc2;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: CTTry.SNK", port);

		/* Enable PD */
		tc[port].pd_enable = 1;

		/* Enable RX */
		vpd_rx_enable(1);

		tc[port].cc_state = -1;
		tc[port].new_cc_state = -1;
		tc[port].next_role_swap = get_time().val + PD_T_TRY_SRC;
		break;
	case RUN_SIG:
		/*
		 * Wait for tDRPTry before monitoring the Charge-Through
		 * port’s CC pins for the SNK.Rp
		 */
		if (get_time().val < tc[port].next_role_swap)
			STATE_RETURN;

		/* Check CT CC for connection */
		vpd_ct_get_cc(&cc1, &cc2);

		if (cc_is_rp(cc1) || cc_is_rp(cc2))
			tc[port].new_cc_state = PD_CC_DFP_ATTACHED;
		else
			tc[port].new_cc_state = PD_CC_NONE;

		/* Debounce the CT CC state */
		if (tc[port].cc_state != tc[port].new_cc_state) {
			tc[port].cc_state = tc[port].new_cc_state;
			tc[port].cc_debounce = get_time().val + PD_T_DEBOUNCE;
			tc[port].try_wait_debounce =
						get_time().val + PD_T_TRY_WAIT;
			STATE_RETURN;
		}

		if (get_time().val > tc[port].cc_debounce) {
			/* Debounce complete */
			if (tc[port].cc_state == PD_CC_DFP_ATTACHED &&
					vpd_is_ct_vbus())
				SET_STATE(port, TC_OBJ(port),
					tc_state_ct_attached_vpd);
		}

		if (get_time().val > tc[port].try_wait_debounce) {
			/* Debounce complete */
			if (tc[port].cc_state == PD_CC_NONE)
				SET_STATE(port, TC_OBJ(port),
					tc_state_ct_attached_unsupported);
		}

		if (!vpd_is_vconn())
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);

	RUN_SIG_DONE;
	case EXIT_SIG:
		/* Disable PD */
		tc[port].pd_enable = 0;

		/* Disable RX */
		vpd_rx_enable(0);
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_rp3_ct_rd);
}

static unsigned int tc_state_ct_attach_wait_unsupported(int port, int sig)
{
	int cc1;
	int cc2;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: CTAttachWait.Unsupported", port);

		tc[port].cc_state = -1;
		tc[port].new_cc_state = -1;
		break;
	case RUN_SIG:
		/* Check CT CC for connection */
		vpd_ct_get_cc(&cc1, &cc2);

		if (cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_OPEN)
			tc[port].new_cc_state = PD_CC_NONE;
		else if (cc1 == TYPEC_CC_VOLT_RD || cc2 == TYPEC_CC_VOLT_RD)
			tc[port].new_cc_state = PD_CC_DFP_ATTACHED;
		else if (cc1 == TYPEC_CC_VOLT_RA && cc2 == TYPEC_CC_VOLT_RA)
			tc[port].new_cc_state = PD_CC_AUDIO_ACC;

		/* Debounce the cc state */
		if (tc[port].cc_state != tc[port].new_cc_state) {
			tc[port].cc_state = tc[port].new_cc_state;
			tc[port].cc_debounce = get_time().val +
						PD_T_CC_DEBOUNCE;
			STATE_RETURN;
		}

		/* Wait for CC debounce */
		if (get_time().val < tc[port].cc_debounce)
			STATE_RETURN;

		/* Debounce complete */
		if (tc[port].new_cc_state == PD_CC_DFP_ATTACHED)
			SET_STATE(port, TC_OBJ(port), tc_state_ct_try_snk);

		if (tc[port].new_cc_state == PD_CC_NONE)
			SET_STATE(port, TC_OBJ(port),
						tc_state_ct_unattached_vpd);

		if (!vpd_is_vconn())
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_rp3_ct_rpu);
}

static unsigned int tc_state_ct_attached_unsupported(int port, int sig)
{
	int cc1;
	int cc2;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: CTAttached.Unsupported", port);

		/* Get Power from VCONN */
		vpd_vconn_pwr_sel_odl(PWR_VCONN);

		/* Present Billboard device */
		vpd_present_billboard(1);
		break;
	case RUN_SIG:
		/* Check CT CC for connection */
		vpd_ct_get_cc(&cc1, &cc2);

		if ((cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_OPEN) ||
		    (cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_RA) ||
		    (cc1 == TYPEC_CC_VOLT_RA && cc2 == TYPEC_CC_VOLT_OPEN))
			SET_STATE(port, TC_OBJ(port),
						tc_state_ct_unattached_vpd);

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_rp3_ct_rpu);
}

static unsigned int tc_state_ct_unattached_unsupported(int port, int sig)
{
	int cc1;
	int cc2;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: CTUnattached.Unsupported", port);
		/* Enable PD */
		tc[port].pd_enable = 1;

		/* Enable RX */
		vpd_rx_enable(1);

		tc[port].next_role_swap = get_time().val + PD_T_DRP_SRC;
		break;
	case RUN_SIG:
		/* Check CT CC for connection */
		vpd_ct_get_cc(&cc1, &cc2);

		if ((cc1 == TYPEC_CC_VOLT_RD || cc2 == TYPEC_CC_VOLT_RD) ||
				(cc1 == TYPEC_CC_VOLT_RA &&
				cc2 == TYPEC_CC_VOLT_RA))
			SET_STATE(port, TC_OBJ(port),
					tc_state_ct_attach_wait_unsupported);

		if (!vpd_is_vconn())
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);


		if (get_time().val > tc[port].next_role_swap)
			SET_STATE(port, TC_OBJ(port),
						tc_state_ct_unattached_vpd);

	RUN_SIG_DONE;
	case EXIT_SIG:
		/* Disable PD */
		tc[port].pd_enable = 0;

		/* Disable RX */
		vpd_rx_enable(0);
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_rp3_ct_rpu);
}

static unsigned int tc_state_ct_unattached_vpd(int port, int sig)
{
	int cc1;
	int cc2;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: CTUnattached.VPD", port);

		/* Enable PD */
		tc[port].pd_enable = 1;

		/* Enable RX */
		vpd_rx_enable(1);

		tc[port].cc_state = -1;
		tc[port].new_cc_state = -1;
		break;
	case RUN_SIG:
		/* Check CT CC for connection */
		vpd_ct_get_cc(&cc1, &cc2);

		/*
		 * Transition to Unattached.SRC SNK.Rp state is detected on
		 * exactly one of the the Charge-Through CC1 or CC2 pins for
		 * at least tCCDebounce and VBUS is detected
		 */
		if ((cc_is_rp(cc1) && !cc_is_rp(cc2)) ||
					(!cc_is_rp(cc1) && cc_is_rp(cc2)))
			tc[port].new_cc_state = PD_CC_DFP_ATTACHED;
		else if (!cc_is_rp(cc1) && !cc_is_rp(cc2))
			tc[port].new_cc_state = PD_CC_NONE;
		else
			tc[port].new_cc_state = -1;

		if (tc[port].new_cc_state == PD_CC_DFP_ATTACHED)
			SET_STATE(port, TC_OBJ(port),
						tc_state_ct_attach_wait_vpd);

		if (!vpd_is_vconn())
			SET_STATE(port, TC_OBJ(port),
					tc_state_ct_unattached_unsupported);

		/* Debounce the cc state */
		if (tc[port].new_cc_state != tc[port].cc_state) {
			tc[port].cc_state = tc[port].new_cc_state;
			tc[port].cc_debounce = get_time().val + PD_T_DRP_SRC;
			STATE_RETURN;
		}

		if (get_time().val < tc[port].cc_debounce)
			STATE_RETURN;

		/* Debounce complete */
		if (tc[port].cc_state == PD_CC_NONE)
			SET_STATE(port, TC_OBJ(port),
				tc_state_ct_unattached_unsupported);

	RUN_SIG_DONE;
	case EXIT_SIG:
		/* Disable PD */
		tc[port].pd_enable = 0;

		/* Disable RX */
		vpd_rx_enable(0);
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_rp3_ct_rd);
}

static unsigned int tc_state_ct_disabled_vpd(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: CTDisable.VPD", port);
		/* Get power from VBUS */
		vpd_vconn_pwr_sel_odl(PWR_VBUS);

		/*
		 * Set CC1_RPUSB_ODH and CC2_RPUSB_ODH to Hi-Z for
		 * Charge-Through CC1 and CC2.
		 */
		vpd_config_cc1_rpusb_odh(PIN_ADC, 0);
		vpd_config_cc2_rpusb_odh(PIN_ADC, 0);

		/*
		 * Set CC1_RP1A5_odh AND CC2_RP1A5_ODH to Hi-Z for
		 * Charge-Through CC1 and CC2.
		 */
		vpd_config_cc1_rp1a5_odh(PIN_ADC, 0);
		vpd_config_cc2_rp1a5_odh(PIN_ADC, 0);

		/* Set CC_VPDMCU as Hi-Z for Host CC */
		vpd_config_cc_vpdmcu(PIN_ADC, 0);

		/* Isolate the Host-side port from the Charge-Through port */
		vpd_vbus_pass_en(0);
		vpd_ct_cc_sel(CT_OPEN);

		tc[port].cc_debounce = get_time().val + PD_T_VPDDISABLE;
		break;
	case RUN_SIG:
		if (get_time().val > tc[port].cc_debounce)
			SET_STATE(port, TC_OBJ(port), tc_state_unattached_snk);

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* no super state */
	return 0;
}

static unsigned int tc_state_ct_attached_vpd(int port, int sig)
{
	int cc1;
	int cc2;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: CTAttached.VPD", port);

		/* Get power from VCONN */
		vpd_vconn_pwr_sel_odl(PWR_VCONN);

		/*
		 * Detect which of the Charge-Through port’s CC1 or CC2
		 * pins is connected through the cable
		 */
		vpd_ct_get_cc(&cc1, &cc2);
		tc[port].ct_cc  = cc_is_rp(cc2) ? 1 : 0;

		/*
		 * 1. Remove or reduce any additional capacitance on the
		 *    Host-side CC port
		 */
		vpd_mcu_cc_en(0);

		/*
		 * 2. Disable the Rp termination advertising 3.0 A on the
		 *    host port’s CC pin
		 */
		vpd_host_set_pull(TYPEC_CC_OPEN, 0);

		/*
		 * 3. Passively multiplex the detected Charge-Through port’s
		 *    CC pin through to the host port’s CC
		 */
		vpd_ct_cc_sel(tc[port].ct_cc ? CT_CC2 : CT_CC1);

		/*
		 * 4. Disable the Rd on the Charge-Through port’s CC1 and CC2
		 *    pins
		 */
		vpd_ct_set_pull(TYPEC_CC_OPEN, 0);

		/*
		 * 5. Connect the Charge-Through port’s VBUS through to the
		 *    host port’s VBUS
		 */
		vpd_vbus_pass_en(1);

		tc[port].cc_state = -1;
		tc[port].new_cc_state = -1;

		/* Enable interrupt generation on disconnect */
		pd_rx_enable_monitoring(0);
		break;
	case RUN_SIG:
		if (!vpd_is_vconn())
			SET_STATE(port, TC_OBJ(port),
					tc_state_ct_disabled_vpd);

		/* Check CT CC for connection */
		vpd_ct_get_cc(&cc1, &cc2);
		if (tc[port].ct_cc) {
			if (cc2 == TYPEC_CC_VOLT_OPEN)
				tc[port].new_cc_state = PD_CC_NONE;
			else
				tc[port].new_cc_state = PD_CC_DFP_ATTACHED;
		} else {
			if (cc1 == TYPEC_CC_VOLT_OPEN)
				tc[port].new_cc_state = PD_CC_NONE;
			else
				tc[port].new_cc_state = PD_CC_DFP_ATTACHED;
		}

		/* Debounce the cc state */
		if (tc[port].new_cc_state != tc[port].cc_state) {
			tc[port].cc_state = tc[port].new_cc_state;
			tc[port].cc_debounce = get_time().val +
							PD_T_VPDCTDD;
			STATE_RETURN;
		}

		if (get_time().val < tc[port].pd_debounce)
			STATE_RETURN;

		if (tc[port].cc_state == PD_CC_NONE && !vpd_is_ct_vbus())
			SET_STATE(port, TC_OBJ(port),
						tc_state_ct_unattached_vpd);

	RUN_SIG_DONE;
	case EXIT_SIG:
		/* Disable interrupt generation on disconnect */
		pd_rx_disable_monitoring(0);
		break;
	}

	/* no super state */
	return 0;
}
static unsigned int tc_state_ct_attach_wait_vpd(int port, int sig)
{
	int cc1;
	int cc2;

	switch (sig) {
	case ENTRY_SIG:
		CPRINTS("C%d: CTAttachWait.VPD", port);

		/* Enable PD */
		tc[port].pd_enable = 1;

		/* Enable RX */
		vpd_rx_enable(1);

		tc[port].cc_state = -1;
		tc[port].new_cc_state = -1;
		break;
	case RUN_SIG:
		/* Check CT CC for connection */
		vpd_ct_get_cc(&cc1, &cc2);

		if ((cc_is_rp(cc1) && !cc_is_rp(cc2)) ||
					(!cc_is_rp(cc1) && cc_is_rp(cc2)))
			tc[port].new_cc_state = PD_CC_DFP_ATTACHED;
		else if (!cc_is_rp(cc1) && !cc_is_rp(cc2))
			tc[port].new_cc_state = PD_CC_NONE;
		else
			tc[port].new_cc_state =  -1;

		if (!vpd_is_vconn())
			SET_STATE(port, TC_OBJ(port),
					tc_state_ct_disabled_vpd);

		/* Debounce the cc state */
		if (tc[port].new_cc_state != tc[port].cc_state) {
			tc[port].cc_state = tc[port].new_cc_state;
			tc[port].cc_debounce = get_time().val +
							PD_T_CC_DEBOUNCE;
			tc[port].pd_debounce = get_time().val +
							PD_T_PD_DEBOUNCE;
			STATE_RETURN;
		}

		if (get_time().val > tc[port].pd_debounce)
			/* Debounce complete */
			if (tc[port].cc_state  == PD_CC_NONE)
				SET_STATE(port, TC_OBJ(port),
						tc_state_ct_unattached_vpd);

		if (get_time().val > tc[port].cc_debounce)
			/* Debounce complete */
			if (tc[port].cc_state  == PD_CC_DFP_ATTACHED &&
							vpd_is_ct_vbus())
				SET_STATE(port, TC_OBJ(port),
					tc_state_ct_attached_vpd);
	RUN_SIG_DONE;
	case EXIT_SIG:
		/* Disable PD */
		tc[port].pd_enable = 0;

		/* Disable RX */
		vpd_rx_enable(0);
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_host_rp3_ct_rd);
}

static unsigned int tc_state_host_rp3_ct_rd(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Place RP3A0 on Host CC */
		vpd_host_set_pull(TYPEC_CC_RP, TYPEC_RP_3A0);

		/* Connect Charge-Through Rd */
		vpd_ct_set_pull(TYPEC_CC_RD, 0);
		break;
	case RUN_SIG:

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_vbus_cc_iso);
}

static unsigned int tc_state_host_rp3_ct_rpu(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Place RP3A0 on Host CC */
		vpd_host_set_pull(TYPEC_CC_RP, TYPEC_RP_3A0);

		/* Place RPUSB on Charge-Through CC */
		vpd_ct_set_pull(TYPEC_CC_RP, TYPEC_RP_USB);
		break;
	case RUN_SIG:

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	/* return super state */
	return SUPER(sig, tc_state_vbus_cc_iso);
}

#endif /* CONFIG_USB_TYPEC_VPD_CT */
