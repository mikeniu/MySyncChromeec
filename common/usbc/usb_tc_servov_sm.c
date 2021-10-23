/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"

/*
 * USB Type-C DRP module for ServoV4.1
 *   See Figure 4-16 in Release 1.4 of USB Type-C Spec.
 */

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

#define CPRINTF_LX(x, format, args...) \
	do { \
		if (tc_debug_level >= x) \
			CPRINTF(format, ## args); \
	} while (0)
#define CPRINTF_L1(format, args...) CPRINTF_LX(1, format, ## args)
#define CPRINTF_L2(format, args...) CPRINTF_LX(2, format, ## args)
#define CPRINTF_L3(format, args...) CPRINTF_LX(3, format, ## args)

#define CPRINTS_LX(x, format, args...) \
	do { \
		if (tc_debug_level >= x) \
			CPRINTS(format, ## args); \
	} while (0)
#define CPRINTS_L1(format, args...) CPRINTS_LX(1, format, ## args)
#define CPRINTS_L2(format, args...) CPRINTS_LX(2, format, ## args)
#define CPRINTS_L3(format, args...) CPRINTS_LX(3, format, ## args)


/*
 * 15-bit timers running at 5ms
 *
 * Bit 15 is set if the timer is enabled, else its disabled
 * Bits 14:0 are incremented every 5ms for a max time of 163835ms
 */
#define ENABLE_TIMER         0x8000
#define T_LOOP_5MS           (5 * MSEC)

#define TC_T_SRC_RECOVER     (PD_T_SRC_RECOVER / T_LOOP_5MS)
#define TC_T_SAFE_0V         (PD_T_SAFE_0V / T_LOOP_5MS)
#define TC_T_SRC_RECOVER_MAX (PD_T_SRC_RECOVER_MAX / T_LOOP_5MS)
#define TC_T_CC_DEBOUNCE     (PD_T_CC_DEBOUNCE / T_LOOP_5MS)
#define TC_T_PD_DEBOUNCE     (PD_T_PD_DEBOUNCE / T_LOOP_5MS)
#define TC_T_POWER_SUPPLY_TURN_ON_DELAY  33
#define TC_T_ERROR_RECOVERY  (PD_T_ERROR_RECOVERY / T_LOOP_5MS)
#define TC_T_SRC_TURN_ON     (PD_T_PS_SOURCE_ON / T_LOOP_5MS)
#define TC_T_RP_VALUE_CHANGE (PD_T_RP_VALUE_CHANGE / T_LOOP_5MS)
#define TC_T_DRP_SNK         (PD_T_DRP_SNK / T_LOOP_5MS)
#define TC_T_DRP_SRC         (PD_T_DRP_SRC / T_LOOP_5MS)
#define TC_T_SRC_DISCONNECT  (PD_T_SRC_DISCONNECT / T_LOOP_5MS)
#define TC_T_DEBOUNCE        (PD_T_DEBOUNCE / T_LOOP_5MS)


enum timer_t {
	TC_CC_DEBOUNCE = 0,
	TC_PD_DEBOUNCE,
	TC_SRC_RECOVER,
	TC_POWER_SUPPLY_TURN_ON_DELAY,
	TC_ERR_RECOVERY,
	TC_SAFE_0V,
	TC_SRC_RECOVER_MAX,
	TC_SRC_TURN_ON,
	TC_RP_VALUE_CHANGE,
	TC_DRP_SNK,
	TC_DRP_SRC,
	TC_SRC_DISCONNECT,
	TC_DEBOUNCE
};

/* Macros to set, clear, and check TC flags */
#define TC_SET_FLAG(port, flag) atomic_or(&tc[port].flags, (flag))
#define TC_CLR_FLAG(port, flag) atomic_clear_bits(&tc[port].flags, (flag))
#define TC_CHK_FLAG(port, flag) (tc[port].flags & (flag))

/* Type-C Layer Flags */

/* Flag to enable Power Delivery */
#define TC_FLAGS_PD_ENABLE              BIT(0)
/* Flag to give policy control of enabled PD */
#define TC_FLAGS_POLICY_PD_ENABLE       BIT(1)
/* Flag to note port partner has Rp/Rp or Rd/Rd */
#define TC_FLAGS_TS_DTS_PARTNER         BIT(2)
/* Flag to note VBus input has never been low */
#define TC_FLAGS_VBUS_NEVER_LOW         BIT(3)
/* Flag to note request to power role swap */
#define TC_FLAGS_REQUEST_PR_SWAP        BIT(4)
/* Flag to note request to data role swap */
#define TC_FLAGS_REQUEST_DR_SWAP        BIT(5)
/* Flag to note request to power off sink */
#define TC_FLAGS_POWER_OFF_SNK          BIT(6)
/* Flag to note port partner is Power Delivery capable */
#define TC_FLAGS_PARTNER_PD_CAPABLE     BIT(7)
/* Flag to note hard reset has been requested */
#define TC_FLAGS_HARD_RESET_REQUESTED   BIT(8)
/* Flag to note we are currently performing PR Swap */
#define TC_FLAGS_PR_SWAP_IN_PROGRESS    BIT(9)
/* Flag to note we should check for connection */
#define TC_FLAGS_CHECK_CONNECTION       BIT(10)
/* Flag to note request from pd_set_suspend to enter TC_DISABLED state */
#define TC_FLAGS_REQUEST_SUSPEND        BIT(11)
/* Flag to note we are in TC_DISABLED state */
#define TC_FLAGS_SUSPENDED              BIT(12)
/* Flag to indicate the port current limit has changed */
#define TC_FLAGS_UPDATE_CURRENT         BIT(13)
/* Flag for asynchronous call to request Error Recovery */
#define TC_FLAGS_REQUEST_ERROR_RECOVERY	BIT(14)

/* On disconnect, clear most of the flags. */
#define CLR_FLAGS_ON_DISCONNECT(port) TC_CLR_FLAG(port, \
~(TC_FLAGS_REQUEST_SUSPEND | TC_FLAGS_SUSPENDED | TC_FLAGS_POLICY_PD_ENABLE))

enum ps_reset_sequence {
	PS_STATE0,
	PS_STATE1,
	PS_STATE2,
};

/* List of all TypeC-level states */
enum usb_tc_state {
	/* Super States */
	TC_CC_OPEN,
	/* Normal States */
	TC_DISABLED,
	TC_ERROR_RECOVERY,
	TC_UNATTACHED_SNK,
	TC_ATTACH_WAIT_SNK,
	TC_ATTACHED_SNK,
	TC_UNATTACHED_SRC,
	TC_ATTACH_WAIT_SRC,
	TC_ATTACHED_SRC,

	TC_STATE_COUNT,
};
/* Forward declare the full list of states. This is indexed by usb_tc_state */
static const struct usb_state tc_states[];

/*
 * We will use DEBUG LABELS if we will be able to print (COMMON RUNTIME)
 * and either CONFIG_USB_PD_DEBUG_LEVEL is not defined (no override) or
 * we are overriding and the level is not DISABLED.
 *
 * If we can't print or the CONFIG_USB_PD_DEBUG_LEVEL is defined to be 0
 * then the DEBUG LABELS will be removed from the build.
 */
#if defined(CONFIG_COMMON_RUNTIME) && \
	(!defined(CONFIG_USB_PD_DEBUG_LEVEL) || \
	 (CONFIG_USB_PD_DEBUG_LEVEL > 0))
#define USB_PD_DEBUG_LABELS
#endif

/*
 * Helper Macro to determine if the machine is in state
 * TC_ATTACHED_SRC
 */
#define IS_ATTACHED_SRC(port) (get_state_tc(port) == TC_ATTACHED_SRC)

/*
 * Helper Macro to determine if the machine is in state
 * TC_ATTACHED_SNK
 */
#define IS_ATTACHED_SNK(port) (get_state_tc(port) == TC_ATTACHED_SNK)

/* List of human readable state names for console debugging */
__maybe_unused static __const_data const char * const tc_state_names[] = {
#ifdef USB_PD_DEBUG_LABELS
	[TC_DISABLED] = "Disabled",
	[TC_ERROR_RECOVERY] = "ErrorRecovery",
	[TC_UNATTACHED_SNK] = "Unattached.SNK",
	[TC_ATTACH_WAIT_SNK] = "AttachWait.SNK",
	[TC_ATTACHED_SNK] = "Attached.SNK",
	[TC_UNATTACHED_SRC] = "Unattached.SRC",
	[TC_ATTACH_WAIT_SRC] = "AttachWait.SRC",
	[TC_ATTACHED_SRC] = "Attached.SRC",
	/* Super States */
	[TC_CC_OPEN] = "SS:CC_OPEN",

	[TC_STATE_COUNT] = "",
#endif
};

/* Debug log level - higher number == more log */
#ifdef CONFIG_USB_PD_DEBUG_LEVEL
static const enum debug_level tc_debug_level = CONFIG_USB_PD_DEBUG_LEVEL;
#else
static enum debug_level tc_debug_level = DEBUG_LEVEL_1;
#endif

static struct type_c {
	/* state machine context */
	struct sm_ctx ctx;
	/* port flags, see TC_FLAGS_* */
	atomic_t flags;
	/* current port power role (SOURCE or SINK) */
	enum pd_power_role power_role;
	/* current port data role (DFP or UFP) */
	enum pd_data_role data_role;
	/* Power supply reset sequence during a hard reset */
	enum ps_reset_sequence ps_reset_state;
	/* Port polarity */
	enum tcpc_cc_polarity polarity;
	/* The cc state */
	enum pd_cc_states cc_state;
	/* Voltage on CC pin */
	enum tcpc_cc_voltage_status cc_voltage;
	/* Type-C current */
	typec_current_t typec_curr;
	/* Type-C current change */
	typec_current_t typec_curr_change;
	/* CC Debounce Timer */
	uint16_t cc_debounce_timer;
	/* PD Debounce Timer */
	uint16_t pd_debounce_timer;
	/* Power Supply Turn On Delay Timer */
	uint16_t power_supply_turn_on_delay_timer;
	/* Err Recovery Timer */
	uint16_t err_recovery_timer;
	/* SRC Turn On Timer */
	uint16_t src_turn_on_timer;
	/* RP Value Change Timer */
	uint16_t rp_value_change_timer;
	/* DRP SNK Timer */
	uint16_t drp_snk_timer;
	/* DRP SRC Timer */
	uint16_t drp_src_timer;
	/* SRC Disconnect Timer */
	uint16_t src_disconnect_timer;
	/* Debounce Timer */
	uint16_t debounce_timer;
	/* Safe 0V Timer */
	uint16_t safe_0v_timer;
	/* SRC Recover Max Timer */
	uint16_t src_recover_max_timer;
	/* SRC Recover Timer */
	uint16_t src_recover_timer;

	/* Selected TCPC CC/Rp values */
	enum tcpc_cc_pull select_cc_pull;
	enum tcpc_rp_value select_current_limit_rp;
} tc[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Port dual-role state */
static volatile __maybe_unused
enum pd_dual_role_states drp_state[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[0 ... (CONFIG_USB_PD_PORT_MAX_COUNT - 1)] =
		CONFIG_USB_PD_INITIAL_DRP_STATE};

/* Forward declare common, private functions */
static void sink_power_sub_states(int port);
static void pd_update_dual_role_config(int port);

/* Forward declare common, private functions */
static void set_state_tc(const int port, const enum usb_tc_state new_state);
test_export_static enum usb_tc_state get_state_tc(const int port);
static void sink_stop_drawing_current(int port);
static void init_timers(int port);
static void start_timer(int port, enum timer_t timer);
static void stop_timer(int port, enum timer_t timer);
static bool is_expired_timer(int port, enum timer_t timer);

void pd_update_contract(int port)
{
	if (IS_ATTACHED_SRC(port))
		pd_dpm_request(port, DPM_REQUEST_SRC_CAP_CHANGE);
}

void pd_request_source_voltage(int port, int mv)
{
	pd_set_max_voltage(mv);

	if (IS_ATTACHED_SNK(port))
		pd_dpm_request(port, DPM_REQUEST_NEW_POWER_LEVEL);
	else
		pd_dpm_request(port, DPM_REQUEST_PR_SWAP);

	task_wake(PD_PORT_TO_TASK_ID(port));
}

void pd_set_external_voltage_limit(int port, int mv)
{
	pd_set_max_voltage(mv);

	/* Must be in Attached.SNK when this function is called */
	if (get_state_tc(port) == TC_ATTACHED_SNK)
		pd_dpm_request(port, DPM_REQUEST_NEW_POWER_LEVEL);

	task_wake(PD_PORT_TO_TASK_ID(port));
}

void pd_set_new_power_request(int port)
{
	/* Must be in Attached.SNK when this function is called */
	if (get_state_tc(port) == TC_ATTACHED_SNK)
		pd_dpm_request(port, DPM_REQUEST_NEW_POWER_LEVEL);
}

void tc_request_power_swap(int port)
{
	/*
	 * Must be in Attached.SRC or Attached.SNK
	 */
	if (IS_ATTACHED_SRC(port) || IS_ATTACHED_SNK(port)) {
		TC_SET_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS);

		/* Let tc_pr_swap_complete start the Vbus debounce */
		stop_timer(port, TC_DEBOUNCE);
	}
}

static void tc_policy_pd_enable(int port, int en)
{
	if (en)
		TC_SET_FLAG(port, TC_FLAGS_POLICY_PD_ENABLE);
	else
		TC_CLR_FLAG(port, TC_FLAGS_POLICY_PD_ENABLE);
	CPRINTS("C%d: PD comm policy %sabled", port, en ? "en" : "dis");
}

static void tc_enable_pd(int port, int en)
{
	if (en)
		TC_SET_FLAG(port, TC_FLAGS_PD_ENABLE);
	else
		TC_CLR_FLAG(port, TC_FLAGS_PD_ENABLE);
}

/*
 * Exit all modes due to a detach event
 * Note: this skips the ExitMode VDM steps in the PE because it is assumed the
 * partner is not present to receive them, and the PE will no longer be running.
 */
static void tc_set_modes_exit(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
		pd_dfp_exit_mode(port, TCPCI_MSG_SOP, 0, 0);
		pd_dfp_exit_mode(port, TCPCI_MSG_SOP_PRIME, 0, 0);
		pd_dfp_exit_mode(port, TCPCI_MSG_SOP_PRIME_PRIME, 0, 0);
	}
}

static void tc_detached(int port)
{
	TC_CLR_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
	hook_notify(HOOK_USB_PD_DISCONNECT);
	tc_pd_connection(port, 0);
	tc_set_modes_exit(port);
	prl_set_default_pd_revision(port);

	/* Clear any mux connection on detach */
	if (port == DUT)
		usb_mux_set(port, USB_PD_MUX_NONE,
			USB_SWITCH_DISCONNECT, tc[port].polarity);
}

static inline void pd_set_dual_role_and_event(int port,
				enum pd_dual_role_states state, uint32_t event)
{
	drp_state[port] = state;

	if (event != 0)
		task_set_event(PD_PORT_TO_TASK_ID(port), event);
}

void pd_set_dual_role(int port, enum pd_dual_role_states state)
{
	pd_set_dual_role_and_event(port, state, PD_EVENT_UPDATE_DUAL_ROLE);
}

int pd_comm_is_enabled(int port)
{
	return tc_get_pd_enabled(port);
}

void pd_request_data_swap(int port)
{
	/*
	 * Must be in Attached.SRC, Attached.SNK, when this function
	 * is called
	 */
	if (IS_ATTACHED_SRC(port) || IS_ATTACHED_SNK(port)) {
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

/* Return true if partner port is known to be PD capable. */
bool pd_capable(int port)
{
	return !!TC_CHK_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
}

/*
 * Return true if we transition through Unattached.SNK, but we're still waiting
 * to receive source caps from the partner. This indicates that the PD
 * capabilities are not yet known.
 */
bool pd_waiting_on_partner_src_caps(int port)
{
	return !pd_get_src_cap_cnt(port);
}

enum pd_dual_role_states pd_get_dual_role(int port)
{
	return drp_state[port];
}

#ifdef CONFIG_CMD_PD_DEV_DUMP_INFO
static inline void pd_dev_dump_info(uint16_t dev_id, uint32_t *hash)
{
	int j;

	ccprintf("DevId:%d.%d Hash:", HW_DEV_ID_MAJ(dev_id),
		 HW_DEV_ID_MIN(dev_id));
	for (j = 0; j < PD_RW_HASH_SIZE / 4; j++)
		ccprintf(" %08x ", hash[j]);
	ccprintf("\n");
}
#endif /* CONFIG_CMD_PD_DEV_DUMP_INFO */

const char *tc_get_current_state(int port)
{
	if (IS_ENABLED(USB_PD_DEBUG_LABELS))
		return tc_state_names[get_state_tc(port)];
	else
		return "";
}

uint32_t tc_get_flags(int port)
{
	return tc[port].flags;
}

int tc_is_attached_src(int port)
{
	return IS_ATTACHED_SRC(port);
}

int tc_is_attached_snk(int port)
{
	return IS_ATTACHED_SNK(port);
}

void tc_pd_connection(int port, int en)
{
	if (en)
		TC_SET_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
	else
		TC_CLR_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
}

void tc_pr_swap_complete(int port, bool success)
{
	if (IS_ATTACHED_SNK(port)) {
		/*
		 * Give the ADCs in the TCPC or PPC time to react following
		 * a PS_RDY message received during a SRC to SNK swap.
		 * Note: This is empirically determined, not strictly
		 * part of the USB PD spec.
		 * Note: Swap in progress should not be cleared until the
		 * debounce is completed.
		 */
		start_timer(port, TC_DEBOUNCE);
	} else {
		/* PR Swap is no longer in progress */
		TC_CLR_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS);
	}
}

void tc_prs_src_snk_assert_rd(int port)
{
	/*
	 * Must be in Attached.SRC or UnorientedDebugAccessory.SRC
	 * when this function is called
	 */
	if (IS_ATTACHED_SRC(port)) {
		/*
		 * Transition to Attached.SNK to
		 * DebugAccessory.SNK assert Rd
		 */
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void tc_prs_snk_src_assert_rp(int port)
{
	/*
	 * Must be in Attached.SNK
	 * when this function is called
	 */
	if (IS_ATTACHED_SNK(port)) {
		/*
		 * Transition to Attached.SRC
		 * to assert Rp
		 */
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

/*
 * Hard Reset is being requested.  This should not allow a TC connection
 * to go to an unattached state until the connection is recovered from
 * the hard reset.  It is possible for a Hard Reset to cause a timeout
 * in trying to recover and an additional Hard Reset would be issued.
 * During this entire process it is important that the TC is not allowed
 * to go to an unattached state.
 *
 * Type-C Spec Rev 2.0 section 4.5.2.2.5.2
 * Exiting from Attached.SNK State
 * A port that is not a V CONN-Powered USB Device and is not in the
 * process of a USB PD PR_Swap or a USB PD Hard Reset or a USB PD
 * FR_Swap shall transition to Unattached.SNK
 */
void tc_hard_reset_request(int port)
{
	TC_SET_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void tc_snk_power_off(int port)
{
	if (IS_ATTACHED_SNK(port)) {
		TC_SET_FLAG(port, TC_FLAGS_POWER_OFF_SNK);
		sink_stop_drawing_current(port);
	}
}

int tc_src_power_on(int port)
{
	if (IS_ATTACHED_SRC(port))
		return pd_set_power_supply_ready(port);

	return 0;
}

void tc_src_power_off(int port)
{
	/* Remove VBUS */
	pd_power_supply_reset(port);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
					CHARGE_CEIL_NONE);
}

void pd_set_suspend(int port, int suspend)
{
	if (pd_is_port_enabled(port) == !suspend)
		return;

	/* Track if we are suspended or not */
	if (suspend) {
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_SUSPEND);
	} else {
		TC_CLR_FLAG(port, TC_FLAGS_REQUEST_SUSPEND);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void pd_set_error_recovery(int port)
{
	TC_SET_FLAG(port, TC_FLAGS_REQUEST_ERROR_RECOVERY);
}

int pd_is_port_enabled(int port)
{
	/*
	 * Checking get_state_tc(port) from another task isn't safe since it
	 * can return TC_DISABLED before tc_cc_open_entry and tc_disabled_entry
	 * are complete. So check TC_FLAGS_SUSPENDED instead.
	 */
	return !TC_CHK_FLAG(port, TC_FLAGS_SUSPENDED);
}

int pd_fetch_acc_log_entry(int port)
{
	pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_GET_LOG, NULL, 0);

	return EC_RES_SUCCESS;
}

enum tcpc_cc_polarity pd_get_polarity(int port)
{
	return tc[port].polarity;
}

enum pd_data_role pd_get_data_role(int port)
{
	return tc[port].data_role;
}

enum pd_power_role pd_get_power_role(int port)
{
	return tc[port].power_role;
}

enum pd_cc_states pd_get_task_cc_state(int port)
{
	return tc[port].cc_state;
}

uint8_t pd_get_task_state(int port)
{
	return get_state_tc(port);
}

bool pd_get_vconn_state(int port)
{
	return false;
}

const char *pd_get_task_state_name(int port)
{
	return tc_get_current_state(port);
}

void pd_vbus_low(int port)
{
	TC_CLR_FLAG(port, TC_FLAGS_VBUS_NEVER_LOW);
}

int pd_is_connected(int port)
{
	return (IS_ATTACHED_SRC(port) || IS_ATTACHED_SNK(port));
}

bool pd_is_disconnected(int port)
{
	return !pd_is_connected(port);
}

/*
 * PD functions which query our fixed PDO flags.  Both the source and sink
 * capabilities can present these values, and they should match between the two
 * for compliant partners.
 */
static bool pd_check_fixed_flag(int port, uint32_t flag)
{
	uint32_t fixed_pdo;

	if (pd_get_src_cap_cnt(port) != 0)
		fixed_pdo = *pd_get_src_caps(port);
	else if (pd_get_snk_cap_cnt(port) != 0)
		fixed_pdo = *pd_get_snk_caps(port);
	else
		return false;

	/*
	 * Error check that first PDO is fixed, as 6.4.1 Capabilities requires
	 * in the Power Delivery Specification.
	 * "The vSafe5V Fixed Supply Object Shall always be the first object"
	 */
	if ((fixed_pdo & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
		return false;

	return fixed_pdo & flag;
}

bool pd_get_partner_data_swap_capable(int port)
{
	return pd_check_fixed_flag(port, PDO_FIXED_DATA_SWAP);
}

bool pd_get_partner_usb_comm_capable(int port)
{
	return pd_check_fixed_flag(port, PDO_FIXED_COMM_CAP);
}

bool pd_get_partner_dual_role_power(int port)
{
	return pd_check_fixed_flag(port, PDO_FIXED_DUAL_ROLE);
}

bool pd_get_partner_unconstr_power(int port)
{
	return pd_check_fixed_flag(port, PDO_FIXED_UNCONSTRAINED);
}

/*
 * TCPC CC/Rp management
 */
static void typec_select_pull(int port, enum tcpc_cc_pull pull)
{
	tc[port].select_cc_pull = pull;
}

void typec_select_src_current_limit_rp(int port, enum tcpc_rp_value rp)
{
	tc[port].select_current_limit_rp = rp;
	if (IS_ATTACHED_SRC(port))
		TC_SET_FLAG(port, TC_FLAGS_UPDATE_CURRENT);
}
__overridable int typec_get_default_current_limit_rp(int port)
{
	return CONFIG_USB_PD_PULLUP;
}

static enum tcpc_rp_value typec_get_active_select_rp(int port)
{
	return tc[port].select_current_limit_rp;
}

int typec_update_cc(int port)
{
	int rv;
	enum tcpc_cc_pull pull = tc[port].select_cc_pull;
	enum tcpc_rp_value rp = typec_get_active_select_rp(port);

	rv = tcpm_select_rp_value(port, rp);
	if (rv)
		return rv;

	return tcpm_set_cc(port, pull);
}

/*
 * This function performs a source hard reset. It should be called
 * repeatedly until a true value is returned, signaling that the
 * source hard reset is complete. A false value is returned otherwise.
 */
static bool tc_perform_src_hard_reset(int port)
{
	switch (tc[port].ps_reset_state) {
	case PS_STATE0:
		/* Remove VBUS */
		tc_src_power_off(port);

		/* Set role to DFP */
		tc_set_data_role(port, PD_ROLE_DFP);

		tc[port].ps_reset_state = PS_STATE1;
		start_timer(port, TC_SRC_RECOVER);
		return false;
	case PS_STATE1:
		/* Enable VBUS */
		tc_src_power_on(port);

		/* Update the Rp Value */
		typec_update_cc(port);

		tc[port].ps_reset_state = PS_STATE2;
		start_timer(port, TC_POWER_SUPPLY_TURN_ON_DELAY);
		return false;
	case PS_STATE2:
		/* Tell Policy Engine Hard Reset is complete */
		pe_ps_reset_complete(port);
		tc[port].ps_reset_state = PS_STATE0;
		return true;
	}

	/*
	 * This return is added to appease the compiler. It should
	 * never be reached because the switch handles all possible
	 * cases of the enum ps_reset_sequence type.
	 */
	return true;
}

/*
 * Wait for recovery after a hard reset.  Call repeatedly until true is
 * returned, signaling that the hard reset is complete.
 */
static bool tc_perform_snk_hard_reset(int port)
{
	switch (tc[port].ps_reset_state) {
	case PS_STATE0:
		/* Hard reset sets us back to default data role */
		tc_set_data_role(port, PD_ROLE_UFP);

		/* Wait up to tVSafe0V for Vbus to disappear */
		tc[port].ps_reset_state = PS_STATE1;
		start_timer(port, TC_SAFE_0V);
		return false;
	case PS_STATE1:
		if (pd_check_vbus_level(port, VBUS_SAFE0V)) {
			/*
			 * Partner dropped Vbus, reduce our current consumption
			 * and await its return.
			 */
			sink_stop_drawing_current(port);

			/* Move on to waiting for the return of Vbus */
			tc[port].ps_reset_state = PS_STATE2;
			start_timer(port, TC_SRC_RECOVER_MAX);
		}

		if (is_expired_timer(port, TC_SAFE_0V)) {
			/*
			 * No Vbus drop likely indicates a non-PD port partner,
			 * move to the next stage anyway.
			 */
			tc[port].ps_reset_state = PS_STATE2;
			start_timer(port, TC_SRC_RECOVER_MAX);
		}
		return false;
	case PS_STATE2:
		/*
		 * Look for the voltage to be above disconnect.  Since we didn't
		 * drop our draw on non-PD partners, they may have dipped below
		 * vSafe5V but still be in a valid connected voltage.
		 */
		if (!pd_check_vbus_level(port, VBUS_REMOVED)) {
			/*
			 * Inform policy engine that power supply
			 * reset is complete
			 */
			tc[port].ps_reset_state = PS_STATE0;
			pe_ps_reset_complete(port);

			/*
			 * Now that VBUS is back, let's notify charge manager
			 * regarding the source's current capabilities.
			 * sink_power_sub_states() reacts to changes in CC
			 * terminations, however during a HardReset, the
			 * terminations of a non-PD port partner will not
			 * change.  Therefore, set the debounce time to right
			 * now, such that we'll actually reset the correct input
			 * current limit.
			 */
			stop_timer(port, TC_CC_DEBOUNCE);
			sink_power_sub_states(port);

			return true;
		}
		/*
		 * If Vbus isn't back after wait + tSrcTurnOn, go unattached
		 */
		if (is_expired_timer(port, TC_SRC_RECOVER_MAX)) {
			tc[port].ps_reset_state = PS_STATE0;
			set_state_tc(port, TC_UNATTACHED_SNK);
			return true;
		}
	}

	return false;
}

void tc_start_error_recovery(int port)
{
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	/*
	 *   The port should transition to the ErrorRecovery state
	 *   from any other state when directed.
	 */
	set_state_tc(port, TC_ERR_RECOVERY);
}

static void restart_tc_sm(int port, enum usb_tc_state start_state)
{
	int res;

	init_timers(port);

	/* Clear flags before we transitions states */
	tc[port].flags = 0;

	res = tcpm_init(port);

	CPRINTS("C%d: TCPC init %s", port, res ? "failed" : "ready");

	/*
	 * Update the Rp Value. We don't need to update CC lines though as that
	 * happens in below set_state transition.
	 */
	typec_select_src_current_limit_rp(port,
		typec_get_default_current_limit_rp(port));

	/* Disable if restart failed, otherwise start in default state. */
	set_state_tc(port, res ? TC_DISABLED : start_state);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		/*
		 * Only initialize PD supplier current limit to 0.
		 * Defer initializing type-C supplier current limit
		 * to Unattached.SNK or Attached.SNK.
		 */
		pd_set_input_current_limit(port, 0, 0);
		charge_manager_update_dualrole(port, CAP_UNKNOWN);
	}

	prl_set_default_pd_revision(port);

	/* Disable PD */
	tc_enable_pd(port, 0);
	tc[port].ps_reset_state = PS_STATE0;
}

void tc_state_init(int port)
{
	/* Turn off any previous sourcing */
	tc_src_power_off(port);

	if (port == DUT)
		restart_tc_sm(port, TC_UNATTACHED_SRC);
	else
		restart_tc_sm(port, TC_UNATTACHED_SNK);
}

enum pd_cable_plug tc_get_cable_plug(int port)
{
	/*
	 * Messages sent by this state machine are always from a DFP/UFP,
	 * i.e. the chromebook.
	 */
	return PD_PLUG_FROM_DFP_UFP;
}

void pd_comm_enable(int port, int en)
{
	tc_policy_pd_enable(port, en);
}

uint8_t tc_get_polarity(int port)
{
	return tc[port].polarity;
}

uint8_t tc_get_pd_enabled(int port)
{
	return TC_CHK_FLAG(port, TC_FLAGS_PD_ENABLE);
}

bool pd_alt_mode_capable(int port)
{
	return false;
}

void tc_set_power_role(int port, enum pd_power_role role)
{
	tc[port].power_role = role;
}

/*
 * Private Functions
 */

static void init_timers(int port)
{
	tc[port].cc_debounce_timer = 0;
	tc[port].pd_debounce_timer = 0;
	tc[port].src_recover_timer = 0;
	tc[port].power_supply_turn_on_delay_timer = 0;
	tc[port].err_recovery_timer = 0;
	tc[port].safe_0v_timer = 0;
	tc[port].src_recover_max_timer = 0;
	tc[port].src_turn_on_timer = 0;
	tc[port].rp_value_change_timer = 0;
	tc[port].drp_snk_timer = 0;
	tc[port].drp_src_timer = 0;
	tc[port].src_disconnect_timer = 0;
	tc[port].debounce_timer = 0;
}

static void stop_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case TC_CC_DEBOUNCE:
		tc[port].cc_debounce_timer = 0;
		break;
	case TC_PD_DEBOUNCE:
		tc[port].pd_debounce_timer = 0;
		break;
	case TC_SRC_RECOVER:
		tc[port].src_recover_timer = 0;
		break;
	case TC_POWER_SUPPLY_TURN_ON_DELAY:
		tc[port].power_supply_turn_on_delay_timer = 0;
		break;
	case TC_ERR_RECOVERY:
		tc[port].err_recovery_timer = 0;
		break;
	case TC_SAFE_0V:
		tc[port].safe_0v_timer = 0;
		break;
	case TC_SRC_RECOVER_MAX:
		tc[port].src_recover_max_timer = 0;
		break;
	case TC_SRC_TURN_ON:
		tc[port].src_turn_on_timer = 0;
		break;
	case TC_RP_VALUE_CHANGE:
		tc[port].rp_value_change_timer = 0;
		break;
	case TC_DRP_SNK:
		tc[port].drp_snk_timer = 0;
		break;
	case TC_DRP_SRC:
		tc[port].drp_src_timer = 0;
		break;
	case TC_SRC_DISCONNECT:
		tc[port].src_disconnect_timer = 0;
		break;
	case TC_DEBOUNCE:
		tc[port].debounce_timer = 0;
		break;
	}
}

static void start_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case TC_CC_DEBOUNCE:
		tc[port].cc_debounce_timer = ENABLE_TIMER | TC_T_CC_DEBOUNCE;
		break;
	case TC_PD_DEBOUNCE:
		tc[port].pd_debounce_timer = ENABLE_TIMER | TC_T_PD_DEBOUNCE;
		break;
	case TC_SRC_RECOVER:
		tc[port].src_recover_timer = ENABLE_TIMER | TC_T_SRC_RECOVER;
		break;
	case TC_POWER_SUPPLY_TURN_ON_DELAY:
		tc[port].power_supply_turn_on_delay_timer =
			ENABLE_TIMER | TC_T_POWER_SUPPLY_TURN_ON_DELAY;
		break;
	case TC_ERR_RECOVERY:
		tc[port].err_recovery_timer =
			ENABLE_TIMER | TC_T_ERROR_RECOVERY;
		break;
	case TC_SAFE_0V:
		tc[port].safe_0v_timer = ENABLE_TIMER | TC_T_SAFE_0V;
		break;
	case TC_SRC_RECOVER_MAX:
		tc[port].src_recover_max_timer =
			ENABLE_TIMER |
			(TC_T_SRC_RECOVER_MAX + TC_T_SRC_TURN_ON);
		break;
	case TC_SRC_TURN_ON:
		tc[port].src_turn_on_timer = ENABLE_TIMER | TC_T_SRC_TURN_ON;
		break;
	case TC_RP_VALUE_CHANGE:
		tc[port].rp_value_change_timer =
			ENABLE_TIMER | TC_T_RP_VALUE_CHANGE;
		break;
	case TC_DRP_SNK:
		tc[port].drp_snk_timer = ENABLE_TIMER | TC_T_DRP_SNK;
		break;
	case TC_DRP_SRC:
		tc[port].drp_src_timer = ENABLE_TIMER | TC_T_DRP_SRC;
		break;
	case TC_SRC_DISCONNECT:
		tc[port].src_disconnect_timer =
			ENABLE_TIMER | TC_T_SRC_DISCONNECT;
		break;
	case TC_DEBOUNCE:
		tc[port].debounce_timer = ENABLE_TIMER | TC_T_DEBOUNCE;
		break;
	}
}

static void update_timers(int port)
{
	if (tc[port].cc_debounce_timer > ENABLE_TIMER)
		tc[port].cc_debounce_timer--;

	if (tc[port].pd_debounce_timer > ENABLE_TIMER)
		tc[port].pd_debounce_timer--;

	if (tc[port].power_supply_turn_on_delay_timer > ENABLE_TIMER)
		tc[port].power_supply_turn_on_delay_timer--;

	if (tc[port].err_recovery_timer > ENABLE_TIMER)
		tc[port].err_recovery_timer--;

	if (tc[port].src_turn_on_timer > ENABLE_TIMER)
		tc[port].src_turn_on_timer--;

	if (tc[port].rp_value_change_timer > ENABLE_TIMER)
		tc[port].rp_value_change_timer--;

	if (tc[port].drp_snk_timer > ENABLE_TIMER)
		tc[port].drp_snk_timer--;

	if (tc[port].drp_src_timer > ENABLE_TIMER)
		tc[port].drp_src_timer--;

	if (tc[port].src_disconnect_timer > ENABLE_TIMER)
		tc[port].src_disconnect_timer--;

	if (tc[port].debounce_timer > ENABLE_TIMER)
		tc[port].debounce_timer--;

	if (tc[port].safe_0v_timer > ENABLE_TIMER)
		tc[port].safe_0v_timer--;

	if (tc[port].src_recover_max_timer > ENABLE_TIMER)
		tc[port].src_recover_max_timer--;

	if (tc[port].src_recover_timer > ENABLE_TIMER)
		tc[port].src_recover_timer--;
}

static bool is_expired_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case TC_CC_DEBOUNCE:
		return (tc[port].cc_debounce_timer == ENABLE_TIMER);
	case TC_PD_DEBOUNCE:
		return (tc[port].pd_debounce_timer == ENABLE_TIMER);
	case TC_POWER_SUPPLY_TURN_ON_DELAY:
		return
		(tc[port].power_supply_turn_on_delay_timer == ENABLE_TIMER);
	case TC_ERR_RECOVERY:
		return (tc[port].err_recovery_timer == ENABLE_TIMER);
	case TC_SRC_TURN_ON:
		return (tc[port].src_turn_on_timer == ENABLE_TIMER);
	case TC_RP_VALUE_CHANGE:
		return (tc[port].rp_value_change_timer == ENABLE_TIMER);
	case TC_DRP_SNK:
		return (tc[port].drp_snk_timer == ENABLE_TIMER);
	case TC_DRP_SRC:
		return (tc[port].drp_src_timer == ENABLE_TIMER);
	case TC_SRC_DISCONNECT:
		return (tc[port].src_disconnect_timer == ENABLE_TIMER);
	case TC_DEBOUNCE:
		return (tc[port].debounce_timer == ENABLE_TIMER);
	case TC_SAFE_0V:
		return (tc[port].safe_0v_timer == ENABLE_TIMER);
	case TC_SRC_RECOVER_MAX:
		return (tc[port].src_recover_max_timer == ENABLE_TIMER);
	case TC_SRC_RECOVER:
		return (tc[port].src_recover_timer == ENABLE_TIMER);
	}

	return true;
}

static bool is_enabled_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case TC_CC_DEBOUNCE:
		return (tc[port].cc_debounce_timer & ENABLE_TIMER);
	case TC_PD_DEBOUNCE:
		return (tc[port].pd_debounce_timer & ENABLE_TIMER);
	case TC_POWER_SUPPLY_TURN_ON_DELAY:
		return
		(tc[port].power_supply_turn_on_delay_timer & ENABLE_TIMER);
	case TC_ERR_RECOVERY:
		return (tc[port].err_recovery_timer & ENABLE_TIMER);
	case TC_SRC_TURN_ON:
		return (tc[port].src_turn_on_timer & ENABLE_TIMER);
	case TC_RP_VALUE_CHANGE:
		return (tc[port].rp_value_change_timer & ENABLE_TIMER);
	case TC_DRP_SNK:
		return (tc[port].drp_snk_timer & ENABLE_TIMER);
	case TC_DRP_SRC:
		return (tc[port].drp_src_timer & ENABLE_TIMER);
	case TC_SRC_DISCONNECT:
		return (tc[port].src_disconnect_timer & ENABLE_TIMER);
	case TC_DEBOUNCE:
		return (tc[port].debounce_timer & ENABLE_TIMER);
	case TC_SAFE_0V:
		return (tc[port].safe_0v_timer & ENABLE_TIMER);
	case TC_SRC_RECOVER_MAX:
		return (tc[port].src_recover_max_timer & ENABLE_TIMER);
	case TC_SRC_RECOVER:
		return (tc[port].src_recover_timer & ENABLE_TIMER);
	}

	return false;
}

/* Set the TypeC state machine to a new state. */
static void set_state_tc(const int port, const enum usb_tc_state new_state)
{
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	set_state(port, &tc[port].ctx, &tc_states[new_state]);
}

/* Get the current TypeC state. */
test_export_static enum usb_tc_state get_state_tc(const int port)
{
	/* Default to returning TC_STATE_COUNT if no state has been set */
	if (tc[port].ctx.current == NULL)
		return TC_STATE_COUNT;
	else
		return tc[port].ctx.current - &tc_states[0];
}

/* Get the previous TypeC state. */
static enum usb_tc_state get_last_state_tc(const int port)
{
	return tc[port].ctx.previous - &tc_states[0];
}

static void print_current_state(const int port)
{
	if (IS_ENABLED(USB_PD_DEBUG_LABELS))
		CPRINTS_L1("C%d: %s", port, tc_state_names[get_state_tc(port)]);
	else
		CPRINTS("C%d: tc-st%d", port, get_state_tc(port));
}

void tc_event_check(int port, int evt)
{
	if (evt & PD_EXIT_LOW_POWER_EVENT_MASK)
		TC_SET_FLAG(port, TC_FLAGS_CHECK_CONNECTION);

	if (evt & PD_EVENT_RX_HARD_RESET)
		pd_execute_hard_reset(port);

	if (evt & PD_EVENT_SEND_HARD_RESET) {
		/* Pass Hard Reset request to PE layer if available */
		if (tc_get_pd_enabled(port))
			pd_dpm_request(port, DPM_REQUEST_HARD_RESET_SEND);
	}

	if (evt & PD_EVENT_UPDATE_DUAL_ROLE)
		pd_update_dual_role_config(port);
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

void tc_set_data_role(int port, enum pd_data_role role)
{
	tc[port].data_role = role;

	if (port == DUT)
		set_usb_mux_with_current_data_role(port);

	/*
	 * Run any board-specific code for role swap (e.g. setting OTG signals
	 * to SoC).
	 */
	pd_execute_data_swap(port, role);

	/* Notify TCPC of role update */
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);
}

static void sink_stop_drawing_current(int port)
{
	pd_set_input_current_limit(port, 0, 0);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		typec_set_input_current_limit(port, 0, 0);
		charge_manager_set_ceil(port,
				CEIL_REQUESTOR_PD, CHARGE_CEIL_NONE);
	}
}

/* This must only be called from the PD task */
static void pd_update_dual_role_config(int port)
{
	if (port == CHG)
		return;

	if (tc[port].power_role == PD_ROLE_SOURCE &&
			(drp_state[port] == PD_DRP_FORCE_SINK ||
			(drp_state[port] == PD_DRP_TOGGLE_OFF &&
			get_state_tc(port) == TC_UNATTACHED_SRC))) {
		/*
		 * Change to sink if port is currently a source AND (new DRP
		 * state is force sink OR new DRP state is toggle off and we are
		 * in the source disconnected state).
		 */
		set_state_tc(port, TC_UNATTACHED_SNK);
	} else if (tc[port].power_role == PD_ROLE_SINK &&
			drp_state[port] == PD_DRP_FORCE_SOURCE) {
		/*
		 * Change to source if port is currently a sink and the
		 * new DRP state is force source.
		 */
		set_state_tc(port, TC_UNATTACHED_SRC);
	}
}

static void sink_power_sub_states(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2, cc;
	enum tcpc_cc_voltage_status new_cc_voltage;

	tcpm_get_cc(port, &cc1, &cc2);

	cc = polarity_rm_dts(tc[port].polarity) ? cc2 : cc1;

	if (cc == TYPEC_CC_VOLT_RP_DEF)
		new_cc_voltage = TYPEC_CC_VOLT_RP_DEF;
	else if (cc == TYPEC_CC_VOLT_RP_1_5)
		new_cc_voltage = TYPEC_CC_VOLT_RP_1_5;
	else if (cc == TYPEC_CC_VOLT_RP_3_0)
		new_cc_voltage = TYPEC_CC_VOLT_RP_3_0;
	else
		new_cc_voltage = TYPEC_CC_VOLT_OPEN;

	/* Debounce the cc state */
	if (new_cc_voltage != tc[port].cc_voltage) {
		tc[port].cc_voltage = new_cc_voltage;
		start_timer(port, TC_CC_DEBOUNCE);
		return;
	}

	if (is_enabled_timer(port, TC_CC_DEBOUNCE)) {
		if (!is_expired_timer(port, TC_CC_DEBOUNCE))
			return;

		stop_timer(port, TC_CC_DEBOUNCE);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
			tc[port].typec_curr = usb_get_typec_current_limit(
				tc[port].polarity, cc1, cc2);

			typec_set_input_current_limit(port,
				tc[port].typec_curr, TYPE_C_VOLTAGE);
			charge_manager_update_dualrole(port, CAP_DEDICATED);
		}
	}
}


/*
 * TYPE-C State Implementations
 */

/**
 * Disabled
 *
 * Super State Entry Actions:
 *   Remove the terminations from CC
 *   Set VBUS off
 */
static void tc_disabled_entry(const int port)
{
	print_current_state(port);
	/*
	 * We have completed tc_cc_open_entry (our super state), so set flag
	 * to indicate to pd_is_port_enabled that we are now suspended.
	 */
	TC_SET_FLAG(port, TC_FLAGS_SUSPENDED);
}

static void tc_disabled_run(const int port)
{
	/* If pd_set_suspend clears the request, go to TC_UNATTACHED_SNK/SRC. */
	if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_SUSPEND))
		tc_pause_event_loop(port);
	else
		set_state_tc(port, drp_state[port] == PD_DRP_FORCE_SOURCE ?
			     TC_UNATTACHED_SRC : TC_UNATTACHED_SNK);
}

static void tc_disabled_exit(const int port)
{
	int rv;

	tc_start_event_loop(port);
	TC_CLR_FLAG(port, TC_FLAGS_SUSPENDED);

	rv = tcpm_init(port);
	CPRINTS("C%d: TCPC init %s", port, rv ? "failed!" : "ready");
}

/**
 * ErrorRecovery
 *
 * Super State Entry Actions:
 *   Remove the terminations from CC
 *   Set's VBUS off
 */
static void tc_error_recovery_entry(const int port)
{
	print_current_state(port);

	start_timer(port, TC_ERR_RECOVERY);

	TC_CLR_FLAG(port, TC_FLAGS_REQUEST_ERROR_RECOVERY);
}

static void tc_error_recovery_run(const int port)
{
	enum usb_tc_state start_state;

	if (!is_expired_timer(port, TC_ERR_RECOVERY))
		return;

	/*
	 * If we transitioned to error recovery as the first state and we
	 * didn't brown out, we don't need to reinitialized the tc statemachine
	 * because we just did that. So transition to the state directly.
	 */
	if (tc[port].ctx.previous == NULL) {
		set_state_tc(port, drp_state[port] == PD_DRP_FORCE_SOURCE ?
			     TC_UNATTACHED_SRC : TC_UNATTACHED_SNK);
		return;
	}

	/*
	 * If try src support is active (e.g. in S0). Then try to become the
	 * SRC, otherwise we should try to be the sink.
	 */
	start_state = TC_UNATTACHED_SNK;
	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		if (drp_state[port] == PD_DRP_FORCE_SOURCE)
			start_state = TC_UNATTACHED_SRC;

	restart_tc_sm(port, start_state);
}

static void tc_error_recovery_exit(const int port)
{
	stop_timer(port, TC_ERR_RECOVERY);
}

/**
 * Unattached.SNK
 */
static void tc_unattached_snk_entry(const int port)
{
	if (get_last_state_tc(port) != TC_UNATTACHED_SRC) {
		tc_detached(port);
		print_current_state(port);
	}

	/* Set power role to sink */
	tc_set_power_role(port, PD_ROLE_SINK);
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);

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
	typec_select_pull(port, TYPEC_CC_RD);
	typec_select_src_current_limit_rp(port,
		typec_get_default_current_limit_rp(port));
	typec_update_cc(port);

	tc[port].data_role = PD_ROLE_DISCONNECTED;

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_update_dualrole(port, CAP_UNKNOWN);

	/*
	 * Indicate that the port is disconnected so the board
	 * can restore state from any previous data swap.
	 */
	pd_execute_data_swap(port, PD_ROLE_DISCONNECTED);
	start_timer(port, TC_DRP_SNK);

	CLR_FLAGS_ON_DISCONNECT(port);
	tc_enable_pd(port, 0);
}

static void tc_unattached_snk_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
		TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);
		tc_set_data_role(port, PD_ROLE_UFP);
		/* Inform Policy Engine that hard reset is complete */
		pe_ps_reset_complete(port);
	}

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	/*
	 * The port shall transition to AttachWait.SNK when a Source
	 * connection is detected, as indicated by the SNK.Rp state
	 * on at least one of its CC pins.
	 *
	 * A DRP shall transition to Unattached.SRC within tDRPTransition
	 * after the state of both CC pins is SNK.Open for
	 * tDRP − dcSRC.DRP ∙ tDRP.
	 */
	if (cc_is_rp(cc1) || cc_is_rp(cc2)) {
		/* Connection Detected */
		set_state_tc(port, TC_ATTACH_WAIT_SNK);
		return;
	}

	/*
	 * Debounce the CC open status. Some TCPC needs time to get the CC
	 * status valid. Before that, CC open is reported by default. Wait
	 * to make sure the CC is really open. Reuse the role toggle timer.
	 */
	if (!is_expired_timer(port, TC_DRP_SNK))
		return;

	/*
	 * Initialize type-C supplier current limits to 0. The charge
	 * manage is now seeded if it was not.
	 */
	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		typec_set_input_current_limit(port, 0, 0);

	/*
	 * Attempt TCPC auto DRP toggle if it is
	 * not already auto toggling.
	 */
	if ((port == DUT) && drp_state[port] == PD_DRP_TOGGLE_ON) {
		/* DRP Toggle. The timer was checked above. */
		set_state_tc(port, TC_UNATTACHED_SRC);
	}
}

static void tc_unattached_snk_exit(const int port)
{
	stop_timer(port, TC_DRP_SNK);
}

/**
 * AttachWait.SNK
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rd on CC
 *   Set power role to SINK
 */
static void tc_attach_wait_snk_entry(const int port)
{
	print_current_state(port);

	tc[port].cc_state = PD_CC_UNSET;
}

static void tc_attach_wait_snk_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if (cc_is_rp(cc1) && cc_is_rp(cc2) && board_is_dts_port(port))
		new_cc_state = PD_CC_DFP_DEBUG_ACC;
	else if (cc_is_rp(cc1) || cc_is_rp(cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else
		new_cc_state = PD_CC_NONE;

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		start_timer(port, TC_CC_DEBOUNCE);
		start_timer(port, TC_PD_DEBOUNCE);
		tc[port].cc_state = new_cc_state;
		return;
	}

	/*
	 * A DRP shall transition to Unattached.SRC when the state of both
	 * the CC1 and CC2 pins is SNK.Open for at least tPDDebounce, however
	 * when DRP state prevents switch to SRC the next state should be
	 * Unattached.SNK.
	 */
	if (new_cc_state == PD_CC_NONE &&
		is_expired_timer(port, TC_PD_DEBOUNCE)) {
		/* We are detached */
		if ((drp_state[port] == PD_DRP_TOGGLE_OFF
		    || drp_state[port] == PD_DRP_FREEZE
		    || drp_state[port] == PD_DRP_FORCE_SINK) ||
			(port == DUT))
			set_state_tc(port, TC_UNATTACHED_SNK);
		else
			set_state_tc(port, TC_UNATTACHED_SRC);
		return;
	}

	/* Wait for CC debounce */
	if (!is_expired_timer(port, TC_CC_DEBOUNCE))
		return;

	/*
	 * The port shall transition to Attached.SNK after the state of only
	 * one of the CC1 or CC2 pins is SNK.Rp for at least tCCDebounce and
	 * VBUS is detected.
	 *
	 * A DRP that strongly prefers the Source role may optionally
	 * transition to Try.SRC instead of Attached.SNK when the state of only
	 * one CC pin has been SNK.Rp for at least tCCDebounce and VBUS is
	 * detected.
	 *
	 * If the port supports Debug Accessory Mode, the port shall transition
	 * to DebugAccessory.SNK if the state of both the CC1 and CC2 pins is
	 * SNK.Rp for at least tCCDebounce and VBUS is detected.
	 */
	if (pd_is_vbus_present(port)) {
		if (new_cc_state == PD_CC_DFP_ATTACHED) {
			set_state_tc(port, TC_ATTACHED_SNK);
		} else {
			/* new_cc_state is PD_CC_DFP_DEBUG_ACC */
			CPRINTS("C%d: Debug accessory detected", port);
			TC_SET_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
			set_state_tc(port, TC_ATTACHED_SNK);
		}
	}
}

static void tc_attach_wait_snk_exit(const int port)
{
	stop_timer(port, TC_CC_DEBOUNCE);
	stop_timer(port, TC_PD_DEBOUNCE);
}

/**
 * Attached.SNK, shared with Debug Accessory.SNK
 */
static void tc_attached_snk_entry(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	print_current_state(port);

	/*
	 * Known state of attach is SNK.  We need to apply this pull value
	 * to make it set in hardware at the correct time but set the common
	 * pull here.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd.
	 */
	typec_select_pull(port, TYPEC_CC_RD);

	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/* Apply Rd */
		typec_update_cc(port);

		/* Change role to sink */
		tc_set_power_role(port, PD_ROLE_SINK);
		tcpm_set_msg_header(port, tc[port].power_role,
							tc[port].data_role);
	} else {
		/* Get connector orientation */
		tcpm_get_cc(port, &cc1, &cc2);
		tc[port].polarity = get_snk_polarity(cc1, cc2);
		pd_set_polarity(port, tc[port].polarity);

		tc_set_data_role(port, PD_ROLE_UFP);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
			tc[port].typec_curr =
			usb_get_typec_current_limit(tc[port].polarity,
								cc1, cc2);
			typec_set_input_current_limit(port,
					tc[port].typec_curr, TYPE_C_VOLTAGE);
			/*
			 * Start new connections as dedicated until source caps
			 * are received, at which point the PE will update the
			 * flag.
			 */
			charge_manager_update_dualrole(port, CAP_DEDICATED);
		}

		/* Apply Rd */
		typec_update_cc(port);
	}

	stop_timer(port, TC_CC_DEBOUNCE);

	/* Enable PD */
	tc_enable_pd(port, 1);
}

/*
 * Check whether Vbus has been removed on this port, accounting for some Vbus
 * debounce if FRS is enabled.
 *
 * Returns true if a new state was set and the calling run should exit.
 */
static bool tc_snk_check_vbus_removed(const int port)
{
	if (pd_check_vbus_level(port, VBUS_REMOVED)) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return true;
	}

	return false;
}

static void tc_attached_snk_run(const int port)
{
	/*
	 * Perform Hard Reset
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
		/*
		 * Wait to clear the hard reset request until Vbus has returned
		 * to default (or, if it didn't return, we transition to
		 * unattached)
		 */
		if (tc_perform_snk_hard_reset(port))
			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);

		return;
	}

	/*
	 * From 4.5.2.2.5.2 Exiting from Attached.SNK State:
	 *
	 * "A port that is not a Vconn-Powered USB Device and is not in the
	 * process of a USB PD PR_Swap or a USB PD Hard Reset or a USB PD
	 * FR_Swap shall transition to Unattached.SNK within tSinkDisconnect
	 * when Vbus falls below vSinkDisconnect for Vbus operating at or
	 * below 5 V or below vSinkDisconnectPD when negotiated by USB PD
	 * to operate above 5 V."
	 */

	/*
	 * Debounce Vbus before we drop that we are doing a PR_Swap
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS) &&
		is_expired_timer(port, TC_DEBOUNCE)) {
		/* PR Swap is no longer in progress */
		TC_CLR_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS);
		stop_timer(port, TC_DEBOUNCE);
	}

	/*
	 * The sink will be powered off during a power role swap but we don't
	 * want to trigger a disconnect.
	 */
	if (!TC_CHK_FLAG(port, TC_FLAGS_POWER_OFF_SNK) &&
	    !TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/*
		 * Detach detection
		 */
		if (tc_snk_check_vbus_removed(port))
			return;

		if (!pe_is_explicit_contract(port))
			sink_power_sub_states(port);

	}

	/*
	 * PD swap commands
	 */
	if (tc_get_pd_enabled(port) && prl_is_running(port)) {
		/*
		 * Power Role Swap
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP)) {
			/*
			 * We may want to verify partner is applying Rd before
			 * we swap. However, some TCPCs (such as TUSB422) will
			 * not report the correct CC status before VBUS falls to
			 * vSafe0V, so this will be problematic in the FRS case.
			 */
			set_state_tc(port, TC_ATTACHED_SRC);
			return;
		}

		/*
		 * Data Role Swap
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP)) {
			TC_CLR_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP);

			/* Perform Data Role Swap */
			tc_set_data_role(port,
				tc[port].data_role == PD_ROLE_UFP ?
					PD_ROLE_DFP : PD_ROLE_UFP);
		}
	}
}

static void tc_attached_snk_exit(const int port)
{
	/* Clear flags after checking Vconn status */
	TC_CLR_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP | TC_FLAGS_POWER_OFF_SNK);

	/* Stop drawing power */
	sink_stop_drawing_current(port);

	stop_timer(port, TC_CC_DEBOUNCE);
	stop_timer(port, TC_DEBOUNCE);
}

/**
 * Unattached.SRC
 */
static void tc_unattached_src_entry(const int port)
{
	if (get_last_state_tc(port) != TC_UNATTACHED_SNK) {
		tc_detached(port);
		print_current_state(port);
	}

	/* Set power role to source */
	tc_set_power_role(port, PD_ROLE_SOURCE);
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);

	/*
	 * We are in an unattached state and considering to be a SRC
	 * searching for a SNK partner.  We set the CC pull value to
	 * to indicate our intent to be SRC in hopes a partner SNK
	 * will is there to attach to.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rp.
	 *
	 * Restore default current limit Rp.
	 *
	 * Run any debug detaches needed before setting CC, as some TCPCs may
	 * require we set CC Open before changing power roles with a debug
	 * accessory.
	 */
	typec_select_pull(port, TYPEC_CC_RP);
	typec_select_src_current_limit_rp(port,
		typec_get_default_current_limit_rp(port));
	typec_update_cc(port);

	tc[port].data_role = PD_ROLE_DISCONNECTED;


	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_update_dualrole(port, CAP_UNKNOWN);

	CLR_FLAGS_ON_DISCONNECT(port);
	tc_enable_pd(port, 0);
	start_timer(port, TC_DRP_SRC);
}

static void tc_unattached_src_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
		TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);
		tc_set_data_role(port, PD_ROLE_DFP);
		/* Inform Policy Engine that hard reset is complete */
		pe_ps_reset_complete(port);
	}

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	/*
	 * Transition to AttachWait.SRC when:
	 *   1) The SRC.Rd state is detected on either CC1 or CC2 pin or
	 *   2) The SRC.Ra state is detected on both the CC1 and CC2 pins.
	 *
	 * A DRP shall transition to Unattached.SNK within tDRPTransition
	 * after dcSRC.DRP ∙ tDRP
	 */
	if (cc_is_at_least_one_rd(cc1, cc2) || cc_is_audio_acc(cc1, cc2))
		set_state_tc(port, TC_ATTACH_WAIT_SRC);
	else if (is_expired_timer(port, TC_DRP_SRC) &&
		drp_state[port] != PD_DRP_FORCE_SOURCE &&
		drp_state[port] != PD_DRP_FREEZE)
		set_state_tc(port, TC_UNATTACHED_SNK);
}

static void tc_unattached_src_exit(const int port)
{
	stop_timer(port, TC_DRP_SRC);
}

/**
 * AttachWait.SRC
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rp on CC
 *   Set power role to SOURCE
 */
static void tc_attach_wait_src_entry(const int port)
{
	print_current_state(port);

	tc[port].cc_state = PD_CC_UNSET;
}

static void tc_attach_wait_src_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if (cc_is_snk_dbg_acc(cc1, cc2) && board_is_dts_port(port)) {
		/*
		 * Debug accessory.
		 * A debug accessory in a non-DTS port will be
		 * recognized by at_least_one_rd as UFP attached.
		 */
		new_cc_state = PD_CC_UFP_DEBUG_ACC;
	} else if (cc_is_at_least_one_rd(cc1, cc2)) {
		/* UFP attached */
		new_cc_state = PD_CC_UFP_ATTACHED;
	} else if (cc_is_audio_acc(cc1, cc2)) {
		/* AUDIO Accessory not supported. Just ignore */
		new_cc_state = PD_CC_UFP_AUDIO_ACC;
	} else {
		/* No UFP */
		if (drp_state[port] == PD_DRP_FORCE_SOURCE)
			set_state_tc(port, TC_UNATTACHED_SRC);
		else
			set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		start_timer(port, TC_CC_DEBOUNCE);
		tc[port].cc_state = new_cc_state;
		return;
	}

	/* Wait for CC debounce */
	if (!is_expired_timer(port, TC_CC_DEBOUNCE))
		return;

	/*
	 * The port shall transition to Attached.SRC when VBUS is at vSafe0V
	 * and the SRC.Rd state is detected on exactly one of the CC1 or CC2
	 * pins for at least tCCDebounce.
	 *
	 * If the port supports Debug Accessory Mode, it shall transition to
	 * UnorientedDebugAccessory.SRC when VBUS is at vSafe0V and the SRC.Rd
	 * state is detected on both the CC1 and CC2 pins for at least
	 * tCCDebounce.
	 */
	if (pd_check_vbus_level(port, VBUS_SAFE0V)) {
		if (new_cc_state == PD_CC_UFP_ATTACHED) {
			set_state_tc(port, TC_ATTACHED_SRC);
			return;
		} else if (new_cc_state == PD_CC_UFP_DEBUG_ACC) {
			CPRINTS("C%d: Debug accessory detected", port);
			TC_SET_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
			set_state_tc(port, TC_ATTACHED_SRC);
			return;
		}
	}
}

static void tc_attach_wait_src_exit(const int port)
{
	stop_timer(port, TC_CC_DEBOUNCE);
}

/**
 * Attached.SRC, shared with UnorientedDebugAccessory.SRC
 */
static void tc_attached_src_entry(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	print_current_state(port);

	stop_timer(port, TC_POWER_SUPPLY_TURN_ON_DELAY);

	/*
	 * Known state of attach is SRC.  We need to apply this pull value
	 * to make it set in hardware at the correct time but set the common
	 * pull here.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * pulled up through Rp.
	 *
	 * Set selected current limit in the hardware.
	 */
	typec_select_pull(port, TYPEC_CC_RP);
	typec_set_source_current_limit(port, tc[port].select_current_limit_rp);

	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/* Change role to source */
		tc_set_power_role(port, PD_ROLE_SOURCE);
		tcpm_set_msg_header(port,
				tc[port].power_role,
				tc[port].data_role);
		/* Enable VBUS */
		tc_src_power_on(port);

		/* Apply Rp */
		typec_update_cc(port);
	} else {
		/*
		 * Set up CC's, Vconn, and ADD before Vbus, as per
		 * Figure 4-24. DRP Initialization and Connection
		 * Detection in TCPCI r2 v1.2 specification.
		 */

		/* Get connector orientation */
		tcpm_get_cc(port, &cc1, &cc2);
		tc[port].polarity = board_get_src_dts_polarity(port);
		pd_set_polarity(port, tc[port].polarity);

		/* Apply Rp */
		typec_update_cc(port);

		/*
		 * Initial data role for sink is DFP
		 */
		tc_set_data_role(port, PD_ROLE_DFP);

		/* Enable VBUS */
		tc_src_power_on(port);

		/* Enable PD */
		tc_enable_pd(port, 1);
	}

	/* Initialize type-C supplier to seed the charge manger */
	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		typec_set_input_current_limit(port, 0, 0);

	/*
	 * Some TCPCs require time to correctly return CC status after
	 * changing the ROLE_CONTROL register. Due to that, we have to ignore
	 * CC_NONE state until PD_T_SRC_DISCONNECT delay has elapsed.
	 * From the "Universal Serial Bus Type-C Cable and Connector
	 * Specification" Release 2.0 paragraph 4.5.2.2.9.2:
	 * The Source shall detect the SRC.Open state within tSRCDisconnect,
	 * but should detect it as quickly as possible
	 */
	start_timer(port, TC_SRC_DISCONNECT);
}

static void tc_attached_src_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if (polarity_rm_dts(tc[port].polarity))
		cc1 = cc2;

	if (cc1 == TYPEC_CC_VOLT_OPEN)
		tc[port].cc_state = PD_CC_NONE;
	else
		tc[port].cc_state = PD_CC_UFP_ATTACHED;

	/*
	 * When the SRC.Open state is detected on the monitored CC pin, a DRP
	 * shall transition to Unattached.SNK unless it strongly prefers the
	 * Source role. In that case, it shall transition to TryWait.SNK.
	 * This transition to TryWait.SNK is needed so that two devices that
	 * both prefer the Source role do not loop endlessly between Source
	 * and Sink. In other words, a DRP that would enter Try.SRC from
	 * AttachWait.SNK shall enter TryWait.SNK for a Sink detach from
	 * Attached.SRC.
	 */
	if (tc[port].cc_state == PD_CC_NONE &&
		is_expired_timer(port, TC_SRC_DISCONNECT)) {
		if (drp_state[port] == PD_DRP_FORCE_SOURCE)
			set_state_tc(port, TC_UNATTACHED_SRC);
		else
			set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/*
	 * Handle Hard Reset from Policy Engine
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
		/* Ignoring Hard Resets while the power supply is resetting.*/
		if (is_enabled_timer(port, TC_POWER_SUPPLY_TURN_ON_DELAY) &&
			!is_expired_timer(port, TC_POWER_SUPPLY_TURN_ON_DELAY))
			return;

		if (tc_perform_src_hard_reset(port))
			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);

		return;
	}

	/*
	 * PD swap commands
	 */
	if (tc_get_pd_enabled(port) && prl_is_running(port)) {
		/*
		 * Power Role Swap Request
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP)) {
			/* Clear TC_FLAGS_REQUEST_PR_SWAP on exit */
			return set_state_tc(port, TC_ATTACHED_SNK);
		}

		/*
		 * Data Role Swap Request
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP)) {
			TC_CLR_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP);

			/* Perform Data Role Swap */
			tc_set_data_role(port,
				tc[port].data_role == PD_ROLE_DFP ?
					PD_ROLE_UFP : PD_ROLE_DFP);
		}
	}

	if (TC_CHK_FLAG(port, TC_FLAGS_UPDATE_CURRENT)) {
		TC_CLR_FLAG(port, TC_FLAGS_UPDATE_CURRENT);
		typec_set_source_current_limit(port,
					tc[port].select_current_limit_rp);
		pd_update_contract(port);

		/* Update Rp if no contract is present */
		if (!pe_is_explicit_contract(port))
			typec_update_cc(port);
	}
}

static void tc_attached_src_exit(const int port)
{
	/*
	 * A port shall cease to supply VBUS within tVBUSOFF of exiting
	 * Attached.SRC.
	 */
	tc_src_power_off(port);

	/* Clear PR swap flag after checking for Vconn */
	TC_CLR_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP);

	stop_timer(port, TC_SRC_DISCONNECT);
	stop_timer(port, TC_POWER_SUPPLY_TURN_ON_DELAY);
}

/**
 * Super State CC_OPEN
 */
static void tc_cc_open_entry(const int port)
{
	/* Ensure we are not sourcing Vbus */
	tc_src_power_off(port);

	/* Remove terminations from CC */
	typec_select_pull(port, TYPEC_CC_OPEN);
	typec_update_cc(port);

	tc_detached(port);
}

void tc_set_debug_level(enum debug_level debug_level)
{
	if (!IS_ENABLED(CONFIG_USB_PD_DEBUG_LEVEL))
		tc_debug_level = debug_level;
}

void tc_run(const int port)
{
	/*
	 * If pd_set_suspend set TC_FLAGS_REQUEST_SUSPEND, go directly to
	 * TC_DISABLED.
	 */
	if (get_state_tc(port) != TC_DISABLED
	    && TC_CHK_FLAG(port, TC_FLAGS_REQUEST_SUSPEND)) {
		pe_invalidate_explicit_contract(port);
		set_state_tc(port, TC_DISABLED);
	}

	/* If error recovery has been requested, transition now */
	if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_ERROR_RECOVERY)) {
		pe_invalidate_explicit_contract(port);
		set_state_tc(port, TC_ERROR_RECOVERY);
	}

	/*Update the timers */
	update_timers(port);

	/* Run the state machine */
	run_state(port, &tc[port].ctx);
}

/*
 * Type-C State Hierarchy (Sub-States are listed inside the boxes)
 *
 * |TC_CC_RD --------------|	|TC_CC_RP ------------------------|
 * |			   |	|				  |
 * |	TC_UNATTACHED_SNK  |	|	TC_UNATTACHED_SRC         |
 * |	TC_ATTACH_WAIT_SNK |	|	TC_ATTACH_WAIT_SRC        |
 * |-----------------------|	|---------------------------------|
 *
 * |TC_CC_OPEN -----------|
 * |                      |
 * |	TC_DISABLED       |
 * |	TC_ERROR_RECOVERY |
 * |----------------------|
 *
 * TC_ATTACHED_SNK    TC_ATTACHED_SRC
 *
 */
static __const_data const struct usb_state tc_states[] = {
	/* Super States */
	[TC_CC_OPEN] = {
		.entry	= tc_cc_open_entry,
	},
	/* Normal States */
	[TC_DISABLED] = {
		.entry	= tc_disabled_entry,
		.run	= tc_disabled_run,
		.exit	= tc_disabled_exit,
		.parent = &tc_states[TC_CC_OPEN],
	},
	[TC_ERROR_RECOVERY] = {
		.entry	= tc_error_recovery_entry,
		.run	= tc_error_recovery_run,
		.exit   = tc_error_recovery_exit,
		.parent = &tc_states[TC_CC_OPEN],
	},
	[TC_UNATTACHED_SNK] = {
		.entry	= tc_unattached_snk_entry,
		.run	= tc_unattached_snk_run,
		.exit	= tc_unattached_snk_exit,
	},
	[TC_ATTACH_WAIT_SNK] = {
		.entry	= tc_attach_wait_snk_entry,
		.run	= tc_attach_wait_snk_run,
		.exit	= tc_attach_wait_snk_exit,
	},
	[TC_ATTACHED_SNK] = {
		.entry	= tc_attached_snk_entry,
		.run	= tc_attached_snk_run,
		.exit	= tc_attached_snk_exit,
	},
	[TC_UNATTACHED_SRC] = {
		.entry	= tc_unattached_src_entry,
		.run	= tc_unattached_src_run,
		.exit	= tc_unattached_src_exit,
	},
	[TC_ATTACH_WAIT_SRC] = {
		.entry	= tc_attach_wait_src_entry,
		.run	= tc_attach_wait_src_run,
		.exit	= tc_attach_wait_src_exit,
	},
	[TC_ATTACHED_SRC] = {
		.entry	= tc_attached_src_entry,
		.run	= tc_attached_src_run,
		.exit	= tc_attached_src_exit,
	},
};
