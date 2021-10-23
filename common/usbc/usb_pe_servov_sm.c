/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "dps.h"
#include "driver/tcpm/tcpm.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "stdbool.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "util.h"
#include "usb_common.h"
#include "usb_pd_dpm.h"
#include "usb_pd_policy.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"
#include "usb_emsg.h"
#include "usb_sm.h"


/*
 * USB Policy Engine Sink / Source module for ServoV4.1
 *
 * Based on Revision 3.0, Version 1.2 of
 * the USB Power Delivery Specification.
 */

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

#define CPRINTF_LX(x, format, args...) \
	do { \
		if (pe_debug_level >= x) \
			CPRINTF(format, ## args); \
	} while (0)
#define CPRINTF_L1(format, args...) CPRINTF_LX(1, format, ## args)
#define CPRINTF_L2(format, args...) CPRINTF_LX(2, format, ## args)
#define CPRINTF_L3(format, args...) CPRINTF_LX(3, format, ## args)

#define CPRINTS_LX(x, format, args...) \
	do { \
		if (pe_debug_level >= x) \
			CPRINTS(format, ## args); \
	} while (0)
#define CPRINTS_L1(format, args...) CPRINTS_LX(1, format, ## args)
#define CPRINTS_L2(format, args...) CPRINTS_LX(2, format, ## args)
#define CPRINTS_L3(format, args...) CPRINTS_LX(3, format, ## args)

#define PE_SET_FLAG(port, flag) atomic_or(&pe[port].flags, (flag))
#define PE_CLR_FLAG(port, flag) atomic_clear_bits(&pe[port].flags, (flag))
#define PE_CHK_FLAG(port, flag) (pe[port].flags & (flag))

/*
 * These macros SET, CLEAR, and CHECK, a DPM (Device Policy Manager)
 * Request. The Requests are listed in usb_pe_sm.h.
 */
#define PE_SET_DPM_REQUEST(port, req) atomic_or(&pe[port].dpm_request, (req))
#define PE_CLR_DPM_REQUEST(port, req) \
	atomic_clear_bits(&pe[port].dpm_request, (req))
#define PE_CHK_DPM_REQUEST(port, req) (pe[port].dpm_request & (req))


/* PE Timers */
/*
 * 15-bit timers running at 5ms
 *
 * Bit 15 is set if the timer is enabled, else its disabled
 * Bits 14:0 are incremented every 5ms for a max time of 163835ms
 */
#define ENABLE_TIMER            0x8000
#define T_LOOP_5MS              (5 * MSEC)

#define PE_T_PS_SOURCE_OFF      (PD_T_PS_SOURCE_OFF / T_LOOP_5MS)
#define PE_T_NO_RESPONSE        (PD_T_NO_RESPONSE / T_LOOP_5MS)
#define PE_T_SENDER_RESPONSE    (PD_T_SENDER_RESPONSE / T_LOOP_5MS)
#define PE_T_PS_TRANSITION      (PD_T_PS_TRANSITION / T_LOOP_5MS)
#define PE_T_PS_SOURCE_ON       (PD_T_PS_SOURCE_ON / T_LOOP_5MS)
#define PE_T_PS_HARD_RESET      (PD_T_PS_HARD_RESET / T_LOOP_5MS)
#define PE_T_VCONN_SOURCE_ON    (PD_T_VCONN_SOURCE_ON / T_LOOP_5MS)
#define PE_T_SINK_REQUEST       (PD_T_SINK_REQUEST / T_LOOP_5MS)
#define PE_T_SWAP_SOURCE_START  (PD_T_SWAP_SOURCE_START / T_LOOP_5MS)
#define PE_T_RP_VALUE_CHANGE    (PD_T_RP_VALUE_CHANGE / T_LOOP_5MS)
#define PE_T_SRC_DISCONNECT     (PD_T_SRC_DISCONNECT / T_LOOP_5MS)
#define PE_T_SRC_TRANSITION     (PD_T_SRC_TRANSITION / T_LOOP_5MS)
#define PE_T_VCONN_STABLE       (PD_T_VCONN_STABLE / T_LOOP_5MS)
#define PE_T_DISCOVER_IDENTITY  (PD_T_DISCOVER_IDENTITY / T_LOOP_5MS)
#define PE_T_PR_SWAP_WAIT       (PD_T_PR_SWAP_WAIT / T_LOOP_5MS)
#define PE_T_SEND_SOURCE_CAP    (PD_T_SEND_SOURCE_CAP / T_LOOP_5MS)
#define PE_T_SINK_WAIT_CAP      (PD_T_SINK_WAIT_CAP / T_LOOP_5MS)

/* VDM Timers ( USB PD Spec Rev2.0 Table 6-30 )*/
#define PE_T_VDM_BUSY           (PD_T_VDM_BUSY / T_LOOP_5MS)
#define PE_T_VDM_E_MODE         (PD_T_VDM_E_MODE / T_LOOP_5MS)
#define PE_T_VDM_RCVR_RSP       (PD_T_VDM_RCVR_RSP / T_LOOP_5MS)
#define PE_T_VDM_SNDR_RSP       (PD_T_VDM_SNDR_RSP / T_LOOP_5MS)
#define PE_T_VDM_WAIT_MODE_E    (PD_T_VDM_WAIT_MODE_E / T_LOOP_5MS)


enum timer_t {
	PE_SENDER_RESPONSE = 0,
	PE_PS_TRANSITION,
	PE_PS_SOURCE_ON,
	PE_PS_SOURCE_OFF,
	PE_PS_HARD_RESET,
	PE_SINK_REQUEST,
	PE_SWAP_SOURCE_START,
	PE_RP_VALUE_CHANGE,
	PE_SRC_DISCONNECT,
	PE_SRC_TRANSITION,
	PE_DISCOVER_IDENTITY,
	PE_PR_SWAP_WAIT,
	PE_SEND_SOURCE_CAP,
	PE_SINK_WAIT_CAP,
	PE_VDM_BUSY,
	PE_VDM_SNDR_RSP,
	PE_NO_RESPONSE
};

/*
 * Policy Engine Layer Flags
 * These are reproduced in test/usb_pe.h. If they change here, they must change
 * there.
 */

/* At least one successful PD communication packet received from port partner */
#define PE_FLAGS_PD_CONNECTION               BIT(0)
/* Accept message received from port partner */
#define PE_FLAGS_ACCEPT                      BIT(1)
/* Power Supply Ready message received from port partner */
#define PE_FLAGS_PS_READY                    BIT(2)
/* Protocol Error was determined based on error recovery current state */
#define PE_FLAGS_PROTOCOL_ERROR              BIT(3)
/* Set if we are in Modal Operation */
#define PE_FLAGS_MODAL_OPERATION             BIT(4)
/* A message we requested to be sent has been transmitted */
#define PE_FLAGS_TX_COMPLETE                 BIT(5)
/* A message sent by a port partner has been received */
#define PE_FLAGS_MSG_RECEIVED                BIT(6)
/* A hard reset has been requested but has not been sent, not currently used */
#define PE_FLAGS_HARD_RESET_PENDING          BIT(7)
/* Port partner sent a Wait message. Wait before we resend our message */
#define PE_FLAGS_WAIT                        BIT(8)
/* An explicit contract is in place with our port partner */
#define PE_FLAGS_EXPLICIT_CONTRACT           BIT(9)
/* Waiting for Sink Capabailities timed out.  Used for retry error handling */
#define PE_FLAGS_SNK_WAIT_CAP_TIMEOUT        BIT(10)
/* Power Supply voltage/current transition timed out */
#define PE_FLAGS_PS_TRANSITION_TIMEOUT       BIT(11)
/* Flag to note current Atomic Message Sequence is interruptible */
#define PE_FLAGS_INTERRUPTIBLE_AMS           BIT(12)
/* Flag to note Power Supply reset has completed */
#define PE_FLAGS_PS_RESET_COMPLETE           BIT(13)
/* VCONN swap operation has completed */
#define PE_FLAGS_VCONN_SWAP_COMPLETE         BIT(14)
/* Flag to note no more setup VDMs (discovery, etc.) should be sent */
#define PE_FLAGS_VDM_SETUP_DONE              BIT(15)
/* Flag to note PR Swap just completed for Startup entry */
#define PE_FLAGS_PR_SWAP_COMPLETE	     BIT(16)
/* Flag to note Port Discovery port partner replied with BUSY */
#define PE_FLAGS_VDM_REQUEST_BUSY            BIT(17)
/* Flag to note Port Discovery port partner replied with NAK */
#define PE_FLAGS_VDM_REQUEST_NAKED           BIT(18)
/* Flag to note FRS/PRS context in shared state machine path */
#define PE_FLAGS_FAST_ROLE_SWAP_PATH         BIT(19)
/* Flag to note if FRS listening is enabled */
#define PE_FLAGS_FAST_ROLE_SWAP_ENABLED      BIT(20)
/* Flag to note TCPC passed on FRS signal from port partner */
#define PE_FLAGS_FAST_ROLE_SWAP_SIGNALED     BIT(21)
/* TODO: POLICY decision: Triggers a DR SWAP attempt from UFP to DFP */
#define PE_FLAGS_DR_SWAP_TO_DFP              BIT(22)
/*
 * TODO: POLICY decision
 * Flag to trigger a message resend after receiving a WAIT from port partner
 */
#define PE_FLAGS_WAITING_PR_SWAP             BIT(23)
/* FLAG is set when an AMS is initiated locally. ie. AP requested a PR_SWAP */
#define PE_FLAGS_LOCALLY_INITIATED_AMS       BIT(24)
/* Flag to note the first message sent in PE_SRC_READY and PE_SNK_READY */
#define PE_FLAGS_FIRST_MSG                   BIT(25)
/* Flag to continue a VDM request if it was interrupted */
#define PE_FLAGS_VDM_REQUEST_CONTINUE        BIT(26)
/* TODO: POLICY decision: Triggers a Vconn SWAP attempt to on */
#define PE_FLAGS_VCONN_SWAP_TO_ON	     BIT(27)
/* FLAG to track that VDM request to port partner timed out */
#define PE_FLAGS_VDM_REQUEST_TIMEOUT	     BIT(28)
/* FLAG to note message was discarded due to incoming message */
#define PE_FLAGS_MSG_DISCARDED		     BIT(29)

#define PE_FLAGS_SEND_SOFT_RESET_ONCE        BIT(30)

/* Message flags which should not persist on returning to ready state */
#define PE_FLAGS_READY_CLR		     (PE_FLAGS_LOCALLY_INITIATED_AMS \
					     | PE_FLAGS_MSG_DISCARDED \
					     | PE_FLAGS_VDM_REQUEST_TIMEOUT \
					     | PE_FLAGS_INTERRUPTIBLE_AMS)

/*
 * Combination to check whether a reply to a message was received.  Our message
 * should have sent (i.e. not been discarded) and a partner message is ready to
 * process.
 *
 * When chunking is disabled (ex. for PD 2.0), these flags will set
 * on the same run cycle.  With chunking, received message will take an
 * additional cycle to be flagged.
 */
#define PE_CHK_REPLY(port)	(PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED) && \
				 !PE_CHK_FLAG(port, PE_FLAGS_MSG_DISCARDED))

/* 6.7.3 Hard Reset Counter */
#define N_HARD_RESET_COUNT 2

/* 6.7.4 Capabilities Counter */
#define N_CAPS_COUNT 25

/*
 * Counter to track how many times to attempt SRC to SNK PR swaps before giving
 * up.
 *
 * Note: This is not a part of power delivery specification
 */
#define N_SNK_SRC_PR_SWAP_COUNT 5

/*
 * Function pointer to a Structured Vendor Defined Message (SVDM) response
 * function defined in the board's usb_pd_policy.c file.
 */
typedef int (*svdm_rsp_func)(int port, uint32_t *payload);

/* List of all Policy Engine level states */
enum usb_pe_state {
	/* Normal States */
	PE_SRC_STARTUP,
	PE_SRC_DISCOVERY,
	PE_SRC_SEND_CAPABILITIES,
	PE_SRC_NEGOTIATE_CAPABILITY,
	PE_SRC_TRANSITION_SUPPLY,
	PE_SRC_READY,
	PE_SRC_DISABLED,
	PE_SRC_CAPABILITY_RESPONSE,
	PE_SRC_HARD_RESET,
	PE_SRC_HARD_RESET_RECEIVED,
	PE_SRC_TRANSITION_TO_DEFAULT,
	PE_SNK_STARTUP,
	PE_SNK_DISCOVERY,
	PE_SNK_WAIT_FOR_CAPABILITIES,
	PE_SNK_EVALUATE_CAPABILITY,
	PE_SNK_SELECT_CAPABILITY,
	PE_SNK_READY,
	PE_SNK_HARD_RESET,
	PE_SNK_TRANSITION_TO_DEFAULT,
	PE_SNK_GIVE_SINK_CAP,
	PE_SNK_GET_SOURCE_CAP,
	PE_SNK_TRANSITION_SINK,
	PE_SEND_SOFT_RESET,
	PE_SOFT_RESET,
	PE_SEND_NOT_SUPPORTED,
	PE_SRC_PING,
	PE_DRS_EVALUATE_SWAP,
	PE_DRS_CHANGE,
	PE_DRS_SEND_SWAP,
	PE_PRS_SRC_SNK_EVALUATE_SWAP,
	PE_PRS_SRC_SNK_TRANSITION_TO_OFF,
	PE_PRS_SRC_SNK_ASSERT_RD,
	PE_PRS_SRC_SNK_WAIT_SOURCE_ON,
	PE_PRS_SRC_SNK_SEND_SWAP,
	PE_PRS_SNK_SRC_EVALUATE_SWAP,
	PE_PRS_SNK_SRC_TRANSITION_TO_OFF,
	PE_PRS_SNK_SRC_ASSERT_RP,
	PE_PRS_SNK_SRC_SOURCE_ON,
	PE_PRS_SNK_SRC_SEND_SWAP,
	PE_VCS_EVALUATE_SWAP,
	PE_VCS_SEND_SWAP,
	PE_VCS_WAIT_FOR_VCONN_SWAP,
	PE_VCS_TURN_ON_VCONN_SWAP,
	PE_VCS_TURN_OFF_VCONN_SWAP,
	PE_VCS_SEND_PS_RDY_SWAP,
	PE_VDM_RESPONSE,
	PE_WAIT_FOR_ERROR_RECOVERY,
	PE_DR_GET_SINK_CAP,
	PE_DR_SNK_GIVE_SOURCE_CAP,
	PE_DR_SRC_GET_SOURCE_CAP,
};

/*
 * The result of a previously sent DPM request; used by PE_VDM_SEND_REQUEST to
 * indicate to child states when they need to handle a response.
 */
enum vdm_response_result {
	/* The parent state is still waiting for a response. */
	VDM_RESULT_WAITING,
	/*
	 * The parent state parsed a message, but there is nothing for the child
	 * to handle, e.g. BUSY.
	 */
	VDM_RESULT_NO_ACTION,
	/* The parent state processed an ACK response. */
	VDM_RESULT_ACK,
	/*
	 * The parent state processed a NAK-like response (NAK, Not Supported,
	 * or response timeout.
	 */
	VDM_RESULT_NAK,
};

/* Forward declare the full list of states. This is indexed by usb_pe_state */
static const struct usb_state pe_states[];

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

/* List of human readable state names for console debugging */
__maybe_unused static __const_data const char * const pe_state_names[] = {
	/* Normal States */
	[PE_SRC_STARTUP] = "PE_SRC_Startup",
	[PE_SRC_DISCOVERY] = "PE_SRC_Discovery",
	[PE_SRC_SEND_CAPABILITIES] = "PE_SRC_Send_Capabilities",
	[PE_SRC_NEGOTIATE_CAPABILITY] = "PE_SRC_Negotiate_Capability",
	[PE_SRC_TRANSITION_SUPPLY] = "PE_SRC_Transition_Supply",
	[PE_SRC_READY] = "PE_SRC_Ready",
	[PE_SRC_DISABLED] = "PE_SRC_Disabled",
	[PE_SRC_CAPABILITY_RESPONSE] = "PE_SRC_Capability_Response",
	[PE_SRC_HARD_RESET] = "PE_SRC_Hard_Reset",
	[PE_SRC_HARD_RESET_RECEIVED] = "PE_SRC_Hard_Reset_Received",
	[PE_SRC_TRANSITION_TO_DEFAULT] = "PE_SRC_Transition_to_default",
	[PE_SNK_STARTUP] = "PE_SNK_Startup",
	[PE_SNK_DISCOVERY] = "PE_SNK_Discovery",
	[PE_SNK_WAIT_FOR_CAPABILITIES] = "PE_SNK_Wait_for_Capabilities",
	[PE_SNK_EVALUATE_CAPABILITY] = "PE_SNK_Evaluate_Capability",
	[PE_SNK_SELECT_CAPABILITY] = "PE_SNK_Select_Capability",
	[PE_SNK_READY] = "PE_SNK_Ready",
	[PE_SNK_HARD_RESET] = "PE_SNK_Hard_Reset",
	[PE_SNK_TRANSITION_TO_DEFAULT] = "PE_SNK_Transition_to_default",
	[PE_SNK_GIVE_SINK_CAP] = "PE_SNK_Give_Sink_Cap",
	[PE_SNK_GET_SOURCE_CAP] = "PE_SNK_Get_Source_Cap",
	[PE_SNK_TRANSITION_SINK] = "PE_SNK_Transition_Sink",
	[PE_SEND_SOFT_RESET] = "PE_Send_Soft_Reset",
	[PE_SOFT_RESET] = "PE_Soft_Reset",
	[PE_SEND_NOT_SUPPORTED] = "PE_Send_Not_Supported",
	[PE_SRC_PING] = "PE_SRC_Ping",
	[PE_DRS_EVALUATE_SWAP] = "PE_DRS_Evaluate_Swap",
	[PE_DRS_CHANGE] = "PE_DRS_Change",
	[PE_DRS_SEND_SWAP] = "PE_DRS_Send_Swap",
	[PE_PRS_SRC_SNK_EVALUATE_SWAP] = "PE_PRS_SRC_SNK_Evaluate_Swap",
	[PE_PRS_SRC_SNK_TRANSITION_TO_OFF] = "PE_PRS_SRC_SNK_Transition_To_Off",
	[PE_PRS_SRC_SNK_ASSERT_RD] = "PE_PRS_SRC_SNK_Assert_Rd",
	[PE_PRS_SRC_SNK_WAIT_SOURCE_ON] = "PE_PRS_SRC_SNK_Wait_Source_On",
	[PE_PRS_SRC_SNK_SEND_SWAP] = "PE_PRS_SRC_SNK_Send_Swap",
	[PE_PRS_SNK_SRC_EVALUATE_SWAP] = "PE_PRS_SNK_SRC_Evaluate_Swap",
	[PE_PRS_SNK_SRC_TRANSITION_TO_OFF] = "PE_PRS_SNK_SRC_Transition_To_Off",
	[PE_PRS_SNK_SRC_ASSERT_RP] = "PE_PRS_SNK_SRC_Assert_Rp",
	[PE_PRS_SNK_SRC_SOURCE_ON] = "PE_PRS_SNK_SRC_Source_On",
	[PE_PRS_SNK_SRC_SEND_SWAP] = "PE_PRS_SNK_SRC_Send_Swap",
	[PE_VDM_RESPONSE] = "PE_VDM_Response",
	[PE_WAIT_FOR_ERROR_RECOVERY] = "PE_Wait_For_Error_Recovery",
	[PE_DR_GET_SINK_CAP] = "PE_DR_Get_Sink_Cap",
	[PE_DR_SNK_GIVE_SOURCE_CAP] = "PE_DR_SNK_Give_Source_Cap",
	[PE_DR_SRC_GET_SOURCE_CAP] = "PE_DR_SRC_Get_Source_Cap",
};

#ifndef CONFIG_USBC_VCONN
GEN_NOT_SUPPORTED(PE_VCS_EVALUATE_SWAP);
#define PE_VCS_EVALUATE_SWAP PE_VCS_EVALUATE_SWAP_NOT_SUPPORTED
GEN_NOT_SUPPORTED(PE_VCS_SEND_SWAP);
#define PE_VCS_SEND_SWAP PE_VCS_SEND_SWAP_NOT_SUPPORTED
GEN_NOT_SUPPORTED(PE_VCS_WAIT_FOR_VCONN_SWAP);
#define PE_VCS_WAIT_FOR_VCONN_SWAP PE_VCS_WAIT_FOR_VCONN_SWAP_NOT_SUPPORTED
GEN_NOT_SUPPORTED(PE_VCS_TURN_ON_VCONN_SWAP);
#define PE_VCS_TURN_ON_VCONN_SWAP PE_VCS_TURN_ON_VCONN_SWAP_NOT_SUPPORTED
GEN_NOT_SUPPORTED(PE_VCS_TURN_OFF_VCONN_SWAP);
#define PE_VCS_TURN_OFF_VCONN_SWAP PE_VCS_TURN_OFF_VCONN_SWAP_NOT_SUPPORTED
GEN_NOT_SUPPORTED(PE_VCS_SEND_PS_RDY_SWAP);
#define PE_VCS_SEND_PS_RDY_SWAP PE_VCS_SEND_PS_RDY_SWAP_NOT_SUPPORTED
#endif /* CONFIG_USBC_VCONN */

static enum sm_local_state local_state[CONFIG_USB_PD_PORT_MAX_COUNT] = {
		SM_PAUSED, SM_PAUSED};

/*
 * Common message send checking
 *
 * PE_MSG_SEND_PENDING:   A message has been requested to be sent.  It has
 *                        not been GoodCRCed or Discarded.
 * PE_MSG_SEND_COMPLETED: The message that was requested has been sent.
 *                        This will only be returned one time and any other
 *                        request for message send status will just return
 *                        PE_MSG_SENT. This message actually includes both
 *                        The COMPLETED and the SENT bit for easier checking.
 *                        NOTE: PE_MSG_SEND_COMPLETED will only be returned
 *                        a single time, directly after TX_COMPLETE.
 * PE_MSG_SENT:           The message that was requested to be sent has
 *                        successfully been transferred to the partner.
 * PE_MSG_DISCARDED:      The message that was requested to be sent was
 *                        discarded.  The partner did not receive it.
 *                        NOTE: PE_MSG_DISCARDED will only be returned
 *                        one time and it is up to the caller to process
 *                        what ever is needed to handle the Discard.
 * PE_MSG_DPM_DISCARDED:  The message that was requested to be sent was
 *                        discarded and an active DRP_REQUEST was active.
 *                        The DRP_REQUEST that was current will be moved
 *                        back to the drp_requests so it can be performed
 *                        later if needed.
 *                        NOTE: PE_MSG_DPM_DISCARDED will only be returned
 *                        one time and it is up to the caller to process
 *                        what ever is needed to handle the Discard.
 */
enum pe_msg_check {
	PE_MSG_SEND_PENDING	= BIT(0),
	PE_MSG_SENT		= BIT(1),
	PE_MSG_DISCARDED	= BIT(2),

	PE_MSG_SEND_COMPLETED	= BIT(3) | PE_MSG_SENT,
	PE_MSG_DPM_DISCARDED	= BIT(4) | PE_MSG_DISCARDED,
};
static void pe_sender_response_msg_entry(const int port);
static enum pe_msg_check pe_sender_response_msg_run(const int port);
static void pe_sender_response_msg_exit(const int port);

/* Debug log level - higher number == more log */
#ifdef CONFIG_USB_PD_DEBUG_LEVEL
static const enum debug_level pe_debug_level = CONFIG_USB_PD_DEBUG_LEVEL;
#else
static enum debug_level pe_debug_level = DEBUG_LEVEL_1;
#endif

/*
 * Policy Engine State Machine Object
 */
static struct policy_engine {
	/* state machine context */
	struct sm_ctx ctx;
	/* current port power role (SOURCE or SINK) */
	enum pd_power_role power_role;
	/* current port data role (DFP or UFP) */
	enum pd_data_role data_role;
	/* state machine flags */
	atomic_t flags;
	/* Device Policy Manager Request */
	atomic_t dpm_request;
	atomic_t dpm_curr_request;
	/* last requested voltage PDO index */
	int requested_idx;

	/*
	 * Port events - PD_STATUS_EVENT_* values
	 * Set from PD task but may be cleared by host command
	 */
	atomic_t events;

	/* port address where soft resets are sent */
	enum tcpci_msg_type soft_reset_sop;

	/* Current limit / voltage based on the last request message */
	uint32_t curr_limit;
	uint32_t supply_voltage;

	/* Partner type to send */
	enum tcpci_msg_type tx_type;

	/* VDM - used to send information to shared VDM Request state */
	uint8_t vdm_cnt;
	uint32_t vdm_data[VDO_HDR_SIZE + VDO_MAX_SIZE];

	/* Timers */
	/* Sender Response Timer */
	uint16_t sender_response_timer;
	/* PS Transition Timer */
	uint16_t ps_transition_timer;
	/* PS Source On Timer */
	uint16_t ps_source_on_timer;
	/* PS Hard Reset Timer */
	uint16_t ps_hard_reset_timer;
	/* Sink Request Timer */
	uint16_t sink_request_timer;
	/* Swap Source Timer */
	uint16_t swap_source_timer;
	/* RP Value Change Timer */
	uint16_t rp_value_change_timer;
	/* SRC Disconnect Timer */
	uint16_t src_disconnect_timer;
	/* SRC Transition Timer */
	uint16_t src_transition_timer;
	/* Discover Identity Timer */
	uint16_t discover_identity_timer;
	/* PR Swap Wait Timer */
	uint16_t pr_swap_wait_timer;
	/* Send Source Cap Timer */
	uint16_t send_source_cap_timer;
	/* Sink Wait Cap Timer */
	uint16_t sink_wait_cap_timer;
	/* VDM Busy Timer */
	uint16_t vdm_busy_timer;
	/* VDM Sender Response Timer */
	uint16_t vdm_sndr_rsp_timer;
	/* No Response Timer */
	uint16_t no_response_timer;
	/* PS Source Off Timer */
	uint16_t ps_source_off_timer;

	/* Counters */

	/*
	 * This counter is used to retry the Hard Reset whenever there is no
	 * response from the remote device.
	 */
	uint8_t hard_reset_counter;

	/*
	 * This counter is used to count the number of Source_Capabilities
	 * Messages which have been sent by a Source at power up or after a
	 * Hard Reset.
	 */
	uint8_t caps_counter;

	/*
	 * This counter maintains a count of Discover Identity Messages sent
	 * to a cable.  If no GoodCRC messages are received after
	 * nDiscoverIdentityCount, the port shall not send any further
	 * SOP'/SOP'' messages.
	 */
	uint8_t discover_identity_counter;
	/*
	 * For PD2.0, we need to be a DFP before sending a discovery identity
	 * message to our port partner. This counter keeps track of how
	 * many attempts to DR SWAP from UFP to DFP.
	 */
	uint8_t dr_swap_attempt_counter;

	/*
	 * This counter tracks how many PR Swap messages are sent when the
	 * partner responds with a Wait message. Only used during SRC to SNK
	 * PR swaps
	 */
	uint8_t src_snk_pr_swap_counter;

	/* Last received source cap */
	uint32_t src_caps[PDO_MAX_OBJECTS];
	int8_t src_cap_cnt; /* -1 on error retrieving source caps */

	/* Last received sink cap */
	uint32_t snk_caps[PDO_MAX_OBJECTS];
	int8_t snk_cap_cnt;
} pe[CONFIG_USB_PD_PORT_MAX_COUNT];

test_export_static enum usb_pe_state get_state_pe(const int port);
test_export_static void set_state_pe(const int port,
				     const enum usb_pe_state new_state);
static void pe_set_dpm_curr_request(const int port, const int request);
static void init_timers(int port);
static void start_timer(int port, enum timer_t timer);
static void stop_timer(int port, enum timer_t timer);
static bool is_expired_timer(int port, enum timer_t timer);
static bool is_enabled_timer(int port, enum timer_t timer);
static void update_timers(int port);

int pd_get_rev(int port, enum tcpci_msg_type type)
{
	return prl_get_rev(port, type);
}

int pd_get_vdo_ver(int port, enum tcpci_msg_type type)
{
	return VDM_VER20;
}

static void pe_set_ready_state(int port)
{
	if (pe[port].power_role == PD_ROLE_SOURCE)
		set_state_pe(port, PE_SRC_READY);
	else
		set_state_pe(port, PE_SNK_READY);
}

static inline void send_data_msg(int port, enum tcpci_msg_type type,
				 enum pd_data_msg_type msg)
{
	/* Clear any previous TX status before sending a new message */
	PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
	prl_send_data_msg(port, type, msg);
}

static __maybe_unused inline void send_ext_data_msg(
	int port, enum tcpci_msg_type type, enum pd_ext_msg_type msg)
{
	/* Clear any previous TX status before sending a new message */
	PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
	prl_send_ext_data_msg(port, type, msg);
}

static inline void send_ctrl_msg(int port, enum tcpci_msg_type type,
				 enum pd_ctrl_msg_type msg)
{
	/* Clear any previous TX status before sending a new message */
	PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
	prl_send_ctrl_msg(port, type, msg);
}

/* Compile-time insurance to ensure this code does not call into prl directly */
#define prl_send_data_msg DO_NOT_USE
#define prl_send_ext_data_msg DO_NOT_USE
#define prl_send_ctrl_msg DO_NOT_USE

static void pe_init(int port)
{
	init_timers(port);
	pe[port].flags = 0;
	pe[port].dpm_request = 0;
	pe[port].dpm_curr_request = 0;
	pe[port].data_role = pd_get_data_role(port);
	pe[port].tx_type = TCPCI_MSG_INVALID;
	pe[port].events = 0;

	tc_pd_connection(port, 0);

	if (pd_get_power_role(port) == PD_ROLE_SOURCE)
		set_state_pe(port, PE_SRC_STARTUP);
	else
		set_state_pe(port, PE_SNK_STARTUP);
}

int pe_is_running(int port)
{
	return local_state[port] == SM_RUN;
}

bool pe_in_local_ams(int port)
{
	return !!PE_CHK_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);
}

void pe_set_debug_level(enum debug_level debug_level)
{
	if (!IS_ENABLED(CONFIG_USB_PD_DEBUG_LEVEL))
		pe_debug_level = debug_level;
}

void pe_run(int port, int evt, int en)
{
	switch (local_state[port]) {
	case SM_PAUSED:
		if (en)
			local_state[port] = SM_INIT;
		break;
	case SM_INIT:
		pe_init(port);
		local_state[port] = SM_RUN;
		/* fall through */
	case SM_RUN:
		if (!en) {
			local_state[port] = SM_PAUSED;
			/*
			 * While we are paused, exit all states and wait until
			 * initialized again.
			 */
			set_state(port, &pe[port].ctx, NULL);
			break;
		}

		/* update PE timers */
		update_timers(port);

		/*
		 * 8.3.3.3.8 PE_SNK_Hard_Reset State
		 * The Policy Engine Shall transition to the PE_SNK_Hard_Reset
		 * state from any state when:
		 * - Hard Reset request from Device Policy Manager
		 *
		 * USB PD specification clearly states that we should go to
		 * PE_SNK_Hard_Reset from ANY state (including states in which
		 * port is source) when DPM requests that. This can lead to
		 * execute Hard Reset path for sink when actually our power
		 * role is source. In our implementation we will choose Hard
		 * Reset path depending on current power role.
		 */
		if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_HARD_RESET_SEND)) {
			pe_set_dpm_curr_request(port,
					DPM_REQUEST_HARD_RESET_SEND);
			if (pd_get_power_role(port) == PD_ROLE_SOURCE)
				set_state_pe(port, PE_SRC_HARD_RESET);
			else
				set_state_pe(port, PE_SNK_HARD_RESET);
		}

		/* Run state machine */
		run_state(port, &pe[port].ctx);
		break;
	}
}

int pe_is_explicit_contract(int port)
{
	return PE_CHK_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT);
}

void pe_message_received(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	PE_SET_FLAG(port, PE_FLAGS_MSG_RECEIVED);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void pe_hard_reset_sent(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	PE_CLR_FLAG(port, PE_FLAGS_HARD_RESET_PENDING);
}

void pe_got_hard_reset(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	/*
	 * Transition from any state to the PE_SRC_Hard_Reset_Received or
	 *  PE_SNK_Transition_to_default state when:
	 *  1) Hard Reset Signaling is detected.
	 */
	pe[port].power_role = pd_get_power_role(port);

	if (pe[port].power_role == PD_ROLE_SOURCE)
		set_state_pe(port, PE_SRC_HARD_RESET_RECEIVED);
	else
		set_state_pe(port, PE_SNK_TRANSITION_TO_DEFAULT);
}

void pe_set_explicit_contract(int port)
{
	PE_SET_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT);
}

void pe_invalidate_explicit_contract(int port)
{
	PE_CLR_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT);
}

void pd_notify_event(int port, uint32_t event_mask)
{
	atomic_or(&pe[port].events, event_mask);

	/* Notify the host that new events are available to read */
	pd_send_host_event(PD_EVENT_TYPEC);
}

void pd_clear_events(int port, uint32_t clear_mask)
{
	atomic_clear_bits(&pe[port].events, clear_mask);
}

uint32_t pd_get_events(int port)
{
	return pe[port].events;
}

void pe_set_snk_caps(int port, int cnt, uint32_t *snk_caps)
{
	pe[port].snk_cap_cnt = cnt;

	memcpy(pe[port].snk_caps, snk_caps, sizeof(uint32_t) * cnt);
}

const uint32_t * const pd_get_snk_caps(int port)
{
	return pe[port].snk_caps;
}

uint8_t pd_get_snk_cap_cnt(int port)
{
	return pe[port].snk_cap_cnt;
}

uint32_t pd_get_requested_voltage(int port)
{
	return pe[port].supply_voltage;
}

uint32_t pd_get_requested_current(int port)
{
	return pe[port].curr_limit;
}

static void pe_send_soft_reset(const int port, enum tcpci_msg_type type)
{
	pe[port].soft_reset_sop = type;
	set_state_pe(port, PE_SEND_SOFT_RESET);
}

void pe_report_discard(int port)
{
	/*
	 * Clear local AMS indicator as our AMS message was discarded, and flag
	 * the discard for the PE
	 */
	PE_CLR_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);
	PE_SET_FLAG(port, PE_FLAGS_MSG_DISCARDED);

	/* TODO(b/157228506): Ensure all states are checking discard */
}

/*
 * Utility function to check for an outgoing message discard during states which
 * send a message as a part of an AMS and wait for the transmit to complete.
 * Note these states should not be power transitioning.
 *
 * In these states, discard due to an incoming message is a protocol error.
 */
static bool pe_check_outgoing_discard(int port)
{
	/*
	 * On outgoing discard, soft reset with SOP* of incoming message
	 *
	 * See Table 6-65 Response to an incoming Message (except VDM) in PD 3.0
	 * Version 2.0 Specification.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_DISCARDED) &&
				PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		enum tcpci_msg_type sop =
				PD_HEADER_GET_SOP(rx_emsg[port].header);

		PE_CLR_FLAG(port, PE_FLAGS_MSG_DISCARDED);
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		pe_send_soft_reset(port, sop);
		return true;
	}

	return false;
}

void pe_report_error(int port, enum pe_error e, enum tcpci_msg_type type)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	/*
	 * If there is a timeout error while waiting for a chunk of a chunked
	 * message, there is no requirement to trigger a soft reset.
	 */
	if (e == ERR_RCH_CHUNK_WAIT_TIMEOUT)
		return;

	/*
	 * Generate Hard Reset if Protocol Error occurred
	 * while in PE_Send_Soft_Reset state.
	 */
	if (get_state_pe(port) == PE_SEND_SOFT_RESET) {
		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SNK_HARD_RESET);
		else
			set_state_pe(port, PE_SRC_HARD_RESET);
		return;
	}

	/*
	 * The following states require custom handling of protocol errors,
	 * because they either need special handling of the no GoodCRC case
	 * (cable identity request, send capabilities), occur before explicit
	 * contract (discovery), or happen during a power transition.
	 *
	 * TODO(b/150774779): TCPMv2: Improve pe_error documentation
	 */
	if ((get_state_pe(port) == PE_SRC_SEND_CAPABILITIES ||
			get_state_pe(port) == PE_SRC_TRANSITION_SUPPLY ||
			get_state_pe(port) == PE_PRS_SNK_SRC_EVALUATE_SWAP ||
			get_state_pe(port) == PE_PRS_SNK_SRC_SOURCE_ON ||
			get_state_pe(port) == PE_PRS_SRC_SNK_WAIT_SOURCE_ON ||
			get_state_pe(port) == PE_SRC_DISABLED ||
			get_state_pe(port) == PE_SRC_DISCOVERY)) {
		PE_SET_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
		task_wake(PD_PORT_TO_TASK_ID(port));
		return;
	}

	/*
	 * See section 8.3.3.4.1.1 PE_SRC_Send_Soft_Reset State:
	 *
	 * The PE_Send_Soft_Reset state shall be entered from
	 * any state when
	 * * A Protocol Error is detected by Protocol Layer during a
	 *   Non-Interruptible AMS or
	 * * A message has not been sent after retries or
	 * * When not in an explicit contract and
	 *   * Protocol Errors occurred on SOP during an Interruptible AMS or
	 *   * Protocol Errors occurred on SOP during any AMS where the first
	 *     Message in the sequence has not yet been sent i.e. an unexpected
	 *     Message is received instead of the expected GoodCRC Message
	 *     response.
	 */
	/* All error types besides transmit errors are Protocol Errors. */
	if ((e != ERR_TCH_XMIT &&
				!PE_CHK_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS))
			|| e == ERR_TCH_XMIT
			|| (!PE_CHK_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT) &&
				type == TCPCI_MSG_SOP)) {
		pe_send_soft_reset(port, type);
	}
	/*
	 * Transition to PE_Snk_Ready or PE_Src_Ready by a Protocol
	 * Error during an Interruptible AMS.
	 */
	else
		pe_set_ready_state(port);
}

void pe_got_soft_reset(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	/*
	 * The PE_SRC_Soft_Reset state Shall be entered from any state when a
	 * Soft_Reset Message is received from the Protocol Layer.
	 */
	set_state_pe(port, PE_SOFT_RESET);
}

__overridable bool pd_can_charge_from_device(int port, const int pdo_cnt,
				      const uint32_t *pdos)
{
	/*
	 * Don't attempt to charge from a device we have no SrcCaps from. Or, if
	 * drp_state is FORCE_SOURCE then don't attempt a PRS.
	 */
	if (pdo_cnt == 0 || pd_get_dual_role(port) == PD_DRP_FORCE_SOURCE)
		return false;

	/*
	 * Treat device as a dedicated charger (meaning we should charge
	 * from it) if:
	 *   - it does not support power swap, or
	 *   - it is unconstrained power, or
	 *   - it presents at least 27 W of available power
	 */

	/* Unconstrained Power or NOT Dual Role Power we can charge from */
	if (pdos[0] & PDO_FIXED_UNCONSTRAINED ||
	    (pdos[0] & PDO_FIXED_DUAL_ROLE) == 0)
		return true;

	/* [virtual] allow_list */
	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		uint32_t max_ma, max_mv, max_pdo, max_mw, unused;

		/*
		 * Get max power that the partner offers (not necessarily what
		 * this board will request)
		 */
		pd_find_pdo_index(pdo_cnt, pdos,
				  PD_REV3_MAX_VOLTAGE,
				  &max_pdo);
		pd_extract_pdo_power(max_pdo, &max_ma, &max_mv, &unused);
		max_mw = max_ma * max_mv / 1000;

		if (max_mw >= PD_DRP_CHARGE_POWER_MIN)
			return true;
	}
	return false;
}

void pd_resume_check_pr_swap_needed(int port)
{
	/*
	 * Explicit contract, current power role of SNK, the device
	 * indicates it should not power us, and device isn't selected
	 * as the charging port (ex. through the GUI) then trigger a PR_Swap
	 */
	if (pe_is_explicit_contract(port) &&
	    pd_get_power_role(port) == PD_ROLE_SINK &&
	    !pd_can_charge_from_device(port, pd_get_src_cap_cnt(port),
				       pd_get_src_caps(port)) &&
	    (!IS_ENABLED(CONFIG_CHARGE_MANAGER) ||
	     charge_manager_get_active_charge_port() != port))
		pd_dpm_request(port, DPM_REQUEST_PR_SWAP);
}

void pd_dpm_request(int port, enum pd_dpm_request req)
{
	PE_SET_DPM_REQUEST(port, req);
}

void pe_vconn_swap_complete(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	PE_SET_FLAG(port, PE_FLAGS_VCONN_SWAP_COMPLETE);
}

void pe_ps_reset_complete(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	PE_SET_FLAG(port, PE_FLAGS_PS_RESET_COMPLETE);
}

void pe_message_sent(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	PE_SET_FLAG(port, PE_FLAGS_TX_COMPLETE);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void pd_send_vdm(int port, uint32_t vid, int cmd, const uint32_t *data,
						int count)
{
	/* Copy VDM Header */
	pe[port].vdm_data[0] =
		VDO(vid, ((vid & USB_SID_PD) == USB_SID_PD) ?  1 :
				(PD_VDO_CMD(cmd) <= CMD_ATTENTION),
			VDO_SVDM_VERS(0) |
				cmd);

	/*
	 * Copy VDOs after the VDM Header. Note that the count refers to VDO
	 * count.
	 */
	memcpy((pe[port].vdm_data + 1), data, count * sizeof(uint32_t));

	pe[port].vdm_cnt = count + 1;

	/*
	 * The PE transmit routine assumes that tx_type was set already. Note,
	 * that this function is likely called from outside the PD task.
	 * (b/180465870)
	 */
	pe[port].tx_type = TCPCI_MSG_SOP;
	pd_dpm_request(port, DPM_REQUEST_VDM);

	task_wake(PD_PORT_TO_TASK_ID(port));
}

void pd_send_hpd(int port, enum hpd_event hpd)
{
	uint32_t data[1];
	int opos = pd_alt_mode(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);

	if (!opos)
		return;

	data[0] = VDO_DP_STATUS((hpd == hpd_irq),  /* IRQ_HPD */
			(hpd != hpd_low),     /* HPD_HI|LOW */
			0,                    /* request exit DP */
			0,                    /* request exit USB */
			0,                    /* MF pref */
			1,                    /* enabled */
			0,                    /* power low */
			0x2);

	pd_send_vdm(port, USB_SID_DISPLAYPORT,
		VDO_OPOS(opos) | CMD_ATTENTION, data, 1);
}

/*
 * Private functions
 */
/* PE Timer API */
static void init_timers(int port)
{
	pe[port].sender_response_timer = 0;
	pe[port].ps_transition_timer = 0;
	pe[port].ps_source_on_timer = 0;
	pe[port].ps_source_off_timer = 0;
	pe[port].ps_hard_reset_timer = 0;
	pe[port].sink_request_timer = 0;
	pe[port].swap_source_timer = 0;
	pe[port].rp_value_change_timer = 0;
	pe[port].src_disconnect_timer = 0;
	pe[port].src_transition_timer = 0;
	pe[port].discover_identity_timer = 0;
	pe[port].pr_swap_wait_timer = 0;
	pe[port].send_source_cap_timer = 0;
	pe[port].sink_wait_cap_timer = 0;
	pe[port].vdm_busy_timer = 0;
	pe[port].vdm_sndr_rsp_timer = 0;
	pe[port].no_response_timer = 0;
}

static void stop_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case PE_SENDER_RESPONSE:
		pe[port].sender_response_timer = 0;
		break;
	case PE_PS_TRANSITION:
		pe[port].ps_transition_timer = 0;
		break;
	case PE_PS_SOURCE_ON:
		pe[port].ps_source_on_timer = 0;
		break;
	case PE_PS_SOURCE_OFF:
		pe[port].ps_source_off_timer = 0;
		break;
	case PE_PS_HARD_RESET:
		pe[port].ps_hard_reset_timer = 0;
		break;
	case PE_SINK_REQUEST:
		pe[port].sink_request_timer = 0;
		break;
	case PE_SWAP_SOURCE_START:
		pe[port].swap_source_timer = 0;
		break;
	case PE_RP_VALUE_CHANGE:
		pe[port].rp_value_change_timer = 0;
		break;
	case PE_SRC_DISCONNECT:
		pe[port].src_disconnect_timer = 0;
		break;
	case PE_SRC_TRANSITION:
		pe[port].src_transition_timer = 0;
		break;
	case PE_DISCOVER_IDENTITY:
		pe[port].discover_identity_timer = 0;
		break;
	case PE_PR_SWAP_WAIT:
		pe[port].pr_swap_wait_timer = 0;
		break;
	case PE_SEND_SOURCE_CAP:
		pe[port].send_source_cap_timer = 0;
		break;
	case PE_SINK_WAIT_CAP:
		pe[port].sink_wait_cap_timer = 0;
		break;
	case PE_VDM_BUSY:
		pe[port].vdm_busy_timer = 0;
		break;
	case PE_VDM_SNDR_RSP:
		pe[port].vdm_sndr_rsp_timer = 0;
		break;
	case PE_NO_RESPONSE:
		pe[port].no_response_timer = 0;
		break;
	}
}

static void start_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case PE_SENDER_RESPONSE:
		pe[port].sender_response_timer =
			ENABLE_TIMER | PE_T_SENDER_RESPONSE;
		break;
	case PE_PS_TRANSITION:
		pe[port].ps_transition_timer =
			ENABLE_TIMER | PE_T_PS_TRANSITION;
		break;
	case PE_PS_SOURCE_ON:
		pe[port].ps_source_on_timer =
			ENABLE_TIMER | PE_T_PS_SOURCE_ON;
		break;
	case PE_PS_HARD_RESET:
		pe[port].ps_hard_reset_timer =
			ENABLE_TIMER | PE_T_PS_HARD_RESET;
		break;
	case PE_SINK_REQUEST:
		pe[port].sink_request_timer =
			ENABLE_TIMER | PE_T_SINK_REQUEST;
		break;
	case PE_SWAP_SOURCE_START:
		pe[port].swap_source_timer =
			ENABLE_TIMER | PE_T_SWAP_SOURCE_START;
		break;
	case PE_RP_VALUE_CHANGE:
		pe[port].rp_value_change_timer =
			ENABLE_TIMER | PE_T_RP_VALUE_CHANGE;
		break;
	case PE_SRC_DISCONNECT:
		pe[port].src_disconnect_timer =
			ENABLE_TIMER | PE_T_SRC_DISCONNECT;
		break;
	case PE_SRC_TRANSITION:
		pe[port].src_transition_timer =
			ENABLE_TIMER | PE_T_SRC_TRANSITION;
		break;
	case PE_DISCOVER_IDENTITY:
		pe[port].discover_identity_timer =
			ENABLE_TIMER | PE_T_DISCOVER_IDENTITY;
		break;
	case PE_PR_SWAP_WAIT:
		pe[port].pr_swap_wait_timer =
			ENABLE_TIMER | PE_T_PR_SWAP_WAIT;
		break;
	case PE_SEND_SOURCE_CAP:
		pe[port].send_source_cap_timer =
			ENABLE_TIMER | PE_T_SEND_SOURCE_CAP;
		break;
	case PE_SINK_WAIT_CAP:
		pe[port].sink_wait_cap_timer =
			ENABLE_TIMER | PE_T_SINK_WAIT_CAP;
		break;
	case PE_VDM_BUSY:
		pe[port].vdm_busy_timer =
			ENABLE_TIMER | PE_T_VDM_BUSY;
		break;
	case PE_VDM_SNDR_RSP:
		pe[port].vdm_sndr_rsp_timer =
			ENABLE_TIMER | PE_T_VDM_SNDR_RSP;
		break;
	case PE_NO_RESPONSE:
		pe[port].no_response_timer =
			ENABLE_TIMER | PE_T_NO_RESPONSE;
		break;
	case PE_PS_SOURCE_OFF:
		pe[port].ps_source_off_timer =
			ENABLE_TIMER | PE_T_PS_SOURCE_OFF;
		break;

	}
}

static void update_timers(int port)
{
	if (pe[port].sender_response_timer > ENABLE_TIMER)
		pe[port].sender_response_timer--;

	if (pe[port].ps_transition_timer > ENABLE_TIMER)
		pe[port].ps_transition_timer--;

	if (pe[port].ps_source_on_timer > ENABLE_TIMER)
		pe[port].ps_source_on_timer--;

	if (pe[port].ps_source_off_timer > ENABLE_TIMER)
		pe[port].ps_source_off_timer--;

	if (pe[port].ps_hard_reset_timer > ENABLE_TIMER)
		pe[port].ps_hard_reset_timer--;

	if (pe[port].sink_request_timer > ENABLE_TIMER)
		pe[port].sink_request_timer--;

	if (pe[port].swap_source_timer > ENABLE_TIMER)
		pe[port].swap_source_timer--;

	if (pe[port].rp_value_change_timer > ENABLE_TIMER)
		pe[port].rp_value_change_timer--;

	if (pe[port].src_disconnect_timer > ENABLE_TIMER)
		pe[port].src_disconnect_timer--;

	if (pe[port].src_transition_timer > ENABLE_TIMER)
		pe[port].src_transition_timer--;

	if (pe[port].discover_identity_timer > ENABLE_TIMER)
		pe[port].discover_identity_timer--;

	if (pe[port].pr_swap_wait_timer  > ENABLE_TIMER)
		pe[port].pr_swap_wait_timer--;

	if (pe[port].send_source_cap_timer > ENABLE_TIMER)
		pe[port].send_source_cap_timer--;

	if (pe[port].sink_wait_cap_timer > ENABLE_TIMER)
		pe[port].sink_wait_cap_timer--;

	if (pe[port].no_response_timer > ENABLE_TIMER)
		pe[port].no_response_timer--;
}

static bool is_expired_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case PE_SENDER_RESPONSE:
		return (pe[port].sender_response_timer == ENABLE_TIMER);
	case PE_PS_TRANSITION:
		return (pe[port].ps_transition_timer == ENABLE_TIMER);
	case PE_PS_SOURCE_ON:
		return (pe[port].ps_source_on_timer == ENABLE_TIMER);
	case PE_PS_SOURCE_OFF:
		return (pe[port].ps_source_off_timer == ENABLE_TIMER);
	case PE_PS_HARD_RESET:
		return (pe[port].ps_hard_reset_timer == ENABLE_TIMER);
	case PE_SINK_REQUEST:
		return (pe[port].sink_request_timer == ENABLE_TIMER);
	case PE_SWAP_SOURCE_START:
		return (pe[port].swap_source_timer == ENABLE_TIMER);
	case PE_RP_VALUE_CHANGE:
		return (pe[port].rp_value_change_timer == ENABLE_TIMER);
	case PE_SRC_DISCONNECT:
		return (pe[port].src_disconnect_timer == ENABLE_TIMER);
	case PE_SRC_TRANSITION:
		return (pe[port].src_transition_timer == ENABLE_TIMER);
	case PE_DISCOVER_IDENTITY:
		return (pe[port].discover_identity_timer == ENABLE_TIMER);
	case PE_PR_SWAP_WAIT:
		return (pe[port].pr_swap_wait_timer == ENABLE_TIMER);
	case PE_SEND_SOURCE_CAP:
		return (pe[port].send_source_cap_timer == ENABLE_TIMER);
	case PE_SINK_WAIT_CAP:
		return (pe[port].sink_wait_cap_timer == ENABLE_TIMER);
	case PE_VDM_BUSY:
		return (pe[port].vdm_busy_timer == ENABLE_TIMER);
	case PE_VDM_SNDR_RSP:
		return (pe[port].vdm_sndr_rsp_timer == ENABLE_TIMER);
	case PE_NO_RESPONSE:
		return (pe[port].no_response_timer == ENABLE_TIMER);
	}

	return true;
}

static bool is_enabled_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case PE_SENDER_RESPONSE:
		return (pe[port].sender_response_timer & ENABLE_TIMER);
	case PE_PS_TRANSITION:
		return (pe[port].ps_transition_timer & ENABLE_TIMER);
	case PE_PS_SOURCE_ON:
		return (pe[port].ps_source_on_timer & ENABLE_TIMER);
	case PE_PS_SOURCE_OFF:
		return (pe[port].ps_source_off_timer & ENABLE_TIMER);
	case PE_PS_HARD_RESET:
		return (pe[port].ps_hard_reset_timer & ENABLE_TIMER);
	case PE_SINK_REQUEST:
		return (pe[port].sink_request_timer & ENABLE_TIMER);
	case PE_SWAP_SOURCE_START:
		return (pe[port].swap_source_timer & ENABLE_TIMER);
	case PE_RP_VALUE_CHANGE:
		return (pe[port].rp_value_change_timer & ENABLE_TIMER);
	case PE_SRC_DISCONNECT:
		return (pe[port].src_disconnect_timer & ENABLE_TIMER);
	case PE_SRC_TRANSITION:
		return (pe[port].src_transition_timer & ENABLE_TIMER);
	case PE_DISCOVER_IDENTITY:
		return (pe[port].discover_identity_timer & ENABLE_TIMER);
	case PE_PR_SWAP_WAIT:
		return (pe[port].pr_swap_wait_timer & ENABLE_TIMER);
	case PE_SEND_SOURCE_CAP:
		return (pe[port].send_source_cap_timer & ENABLE_TIMER);
	case PE_SINK_WAIT_CAP:
		return (pe[port].sink_wait_cap_timer & ENABLE_TIMER);
	case PE_VDM_BUSY:
		return (pe[port].vdm_busy_timer & ENABLE_TIMER);
	case PE_VDM_SNDR_RSP:
		return (pe[port].vdm_sndr_rsp_timer & ENABLE_TIMER);
	case PE_NO_RESPONSE:
		return (pe[port].no_response_timer & ENABLE_TIMER);
	}

	return false;
}

static void pe_set_dpm_curr_request(const int port,
				    const int request)
{
	PE_CLR_DPM_REQUEST(port, request);
	pe[port].dpm_curr_request = request;
}

/* Set the TypeC state machine to a new state. */
test_export_static void set_state_pe(const int port,
				     const enum usb_pe_state new_state)
{
	set_state(port, &pe[port].ctx, &pe_states[new_state]);
}

/* Get the current TypeC state. */
test_export_static enum usb_pe_state get_state_pe(const int port)
{
	return pe[port].ctx.current - &pe_states[0];
}

/*
 * Handle common DPM requests to both source and sink.
 *
 * Note: it is assumed the calling state set PE_FLAGS_LOCALLY_INITIATED_AMS
 *
 * Returns true if state was set and calling run state should now return.
 */
static bool common_src_snk_dpm_requests(int port)
{
	if (IS_ENABLED(CONFIG_USBC_VCONN) &&
			PE_CHK_DPM_REQUEST(port, DPM_REQUEST_VCONN_SWAP)) {
		pe_set_dpm_curr_request(port, DPM_REQUEST_VCONN_SWAP);
		set_state_pe(port, PE_VCS_SEND_SWAP);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_SNK_STARTUP)) {
		pe_set_dpm_curr_request(port, DPM_REQUEST_SNK_STARTUP);
		set_state_pe(port, PE_SNK_STARTUP);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_SRC_STARTUP)) {
		pe_set_dpm_curr_request(port, DPM_REQUEST_SRC_STARTUP);
		set_state_pe(port, PE_SRC_STARTUP);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_SOFT_RESET_SEND)) {
		pe_set_dpm_curr_request(port, DPM_REQUEST_SOFT_RESET_SEND);
		/* Currently only support sending soft reset to SOP */
		pe_send_soft_reset(port, TCPCI_MSG_SOP);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_GET_SNK_CAPS)) {
		pe_set_dpm_curr_request(port,
					DPM_REQUEST_GET_SNK_CAPS);
		set_state_pe(port, PE_DR_GET_SINK_CAP);
		return true;
	}

	return false;
}

/*
 * Handle source-specific DPM requests
 *
 * Returns true if state was set and calling run state should now return.
 */

static bool source_dpm_requests(int port)
{
	/*
	 * Ignore sink-specific request:
	 *   DPM_REQUEST_NEW_POWER_LEVEL
	 *   DPM_REQUEST_SOURCE_CAP
	 */
	PE_CLR_DPM_REQUEST(port, DPM_REQUEST_NEW_POWER_LEVEL |
				 DPM_REQUEST_SOURCE_CAP);

	if (pe[port].dpm_request) {
		uint32_t dpm_request = pe[port].dpm_request;

		PE_SET_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);

		if (PE_CHK_DPM_REQUEST(port,
				       DPM_REQUEST_DR_SWAP)) {
			pe_set_dpm_curr_request(port,
						DPM_REQUEST_DR_SWAP);
			if (PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION))
				set_state_pe(port, PE_SRC_HARD_RESET);
			else
				set_state_pe(port, PE_DRS_SEND_SWAP);
			return true;
		} else if (PE_CHK_DPM_REQUEST(port,
					      DPM_REQUEST_PR_SWAP)) {
			pe_set_dpm_curr_request(port,
						DPM_REQUEST_PR_SWAP);
			set_state_pe(port, PE_PRS_SRC_SNK_SEND_SWAP);
			return true;
		} else if (PE_CHK_DPM_REQUEST(port,
					      DPM_REQUEST_GOTO_MIN)) {
			pe_set_dpm_curr_request(port,
						DPM_REQUEST_GOTO_MIN);
			set_state_pe(port, PE_SRC_TRANSITION_SUPPLY);
			return true;
		} else if (PE_CHK_DPM_REQUEST(port,
					      DPM_REQUEST_SRC_CAP_CHANGE)) {
			pe_set_dpm_curr_request(port,
						DPM_REQUEST_SRC_CAP_CHANGE);
			set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
			return true;
		} else if (PE_CHK_DPM_REQUEST(port,
					      DPM_REQUEST_GET_SRC_CAPS)) {
			pe_set_dpm_curr_request(port,
						DPM_REQUEST_GET_SRC_CAPS);
			set_state_pe(port, PE_DR_SRC_GET_SOURCE_CAP);
			return true;
		} else if (PE_CHK_DPM_REQUEST(port,
					      DPM_REQUEST_SEND_PING)) {
			pe_set_dpm_curr_request(port,
						DPM_REQUEST_SEND_PING);
			set_state_pe(port, PE_SRC_PING);
			return true;
		} else if (common_src_snk_dpm_requests(port)) {
			return true;
		}

		CPRINTF("Unhandled DPM Request %x received\n",
			dpm_request);
		PE_CLR_DPM_REQUEST(port, dpm_request);
		PE_CLR_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);
	}
	return false;
}

/*
 * Handle sink-specific DPM requests
 *
 * Returns true if state was set and calling run state should now return.
 */
static bool sink_dpm_requests(int port)
{
	/*
	 * Ignore source specific requests:
	 *   DPM_REQUEST_GOTO_MIN
	 *   DPM_REQUEST_SRC_CAP_CHANGE,
	 *   DPM_REQUEST_SEND_PING
	 */
	PE_CLR_DPM_REQUEST(port, DPM_REQUEST_GOTO_MIN |
			   DPM_REQUEST_SRC_CAP_CHANGE |
			   DPM_REQUEST_SEND_PING);

	if (pe[port].dpm_request) {
		uint32_t dpm_request = pe[port].dpm_request;

		PE_SET_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);

		if (PE_CHK_DPM_REQUEST(port,
				       DPM_REQUEST_DR_SWAP)) {
			pe_set_dpm_curr_request(port,
						DPM_REQUEST_DR_SWAP);
			if (PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION))
				set_state_pe(port, PE_SNK_HARD_RESET);
			else
				set_state_pe(port, PE_DRS_SEND_SWAP);
			return true;
		} else if (PE_CHK_DPM_REQUEST(port,
					      DPM_REQUEST_PR_SWAP)) {
			pe_set_dpm_curr_request(port,
						DPM_REQUEST_PR_SWAP);
			set_state_pe(port, PE_PRS_SNK_SRC_SEND_SWAP);
			return true;
		} else if (PE_CHK_DPM_REQUEST(port,
					      DPM_REQUEST_SOURCE_CAP)) {
			pe_set_dpm_curr_request(port,
						DPM_REQUEST_SOURCE_CAP);
			set_state_pe(port, PE_SNK_GET_SOURCE_CAP);
			return true;
		} else if (PE_CHK_DPM_REQUEST(port,
					      DPM_REQUEST_NEW_POWER_LEVEL)) {
			pe_set_dpm_curr_request(port,
						DPM_REQUEST_NEW_POWER_LEVEL);
			set_state_pe(port, PE_SNK_SELECT_CAPABILITY);
			return true;
		} else if (common_src_snk_dpm_requests(port)) {
			return true;
		} else {
			CPRINTF("Unhandled DPM Request %x received\n",
				dpm_request);
			PE_CLR_DPM_REQUEST(port, dpm_request);
		}

		PE_CLR_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);
	}
	return false;
}

/* Get the previous TypeC state. */
static enum usb_pe_state get_last_state_pe(const int port)
{
	return pe[port].ctx.previous - &pe_states[0];
}

static void print_current_state(const int port)
{
	const char *mode = "";

	if (IS_ENABLED(USB_PD_DEBUG_LABELS))
		CPRINTS_L1("C%d: %s%s", port,
			pe_state_names[get_state_pe(port)], mode);
	else
		CPRINTS("C%d: pe-st%d", port, get_state_pe(port));
}

__overridable int dpm_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	return charge_manager_get_source_pdo(src_pdo, port);
}

static void send_source_cap(int port)
{
	const uint32_t *src_pdo;
	const int src_pdo_cnt = dpm_get_source_pdo(&src_pdo, port);

	if (src_pdo_cnt == 0) {
		/* No source capabilities defined, sink only */
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_REJECT);
	}

	tx_emsg[port].len = src_pdo_cnt * 4;
	memcpy(tx_emsg[port].buf, (uint8_t *)src_pdo, tx_emsg[port].len);

	send_data_msg(port, TCPCI_MSG_SOP, PD_DATA_SOURCE_CAP);
}

/*
 * Request desired charge voltage from source.
 */
static void pe_send_request_msg(int port)
{
	uint32_t rdo;
	uint32_t curr_limit;
	uint32_t supply_voltage;

	/* Build and send request RDO */
	pd_build_request(PD_VDO_INVALID, &rdo, &curr_limit,
			&supply_voltage, port);

	CPRINTF("C%d: Req [%d] %dmV %dmA", port, RDO_POS(rdo),
					supply_voltage, curr_limit);
	if (rdo & RDO_CAP_MISMATCH)
		CPRINTF(" Mismatch");
	CPRINTF("\n");

	pe[port].curr_limit = curr_limit;
	pe[port].supply_voltage = supply_voltage;

	tx_emsg[port].len = 4;

	memcpy(tx_emsg[port].buf, (uint8_t *)&rdo, tx_emsg[port].len);
	send_data_msg(port, TCPCI_MSG_SOP, PD_DATA_REQUEST);
}

static void pe_update_src_pdo_flags(int port, int pdo_cnt, uint32_t *pdos)
{
	/*
	 * Only parse PDO flags if type is fixed
	 *
	 * Note: From 6.4.1 Capabilities Message "The vSafe5V Fixed Supply
	 * Object Shall always be the first object." so hitting this condition
	 * would mean the partner is voilating spec.
	 */
	if ((pdos[0] & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
		return;

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		if (pd_can_charge_from_device(port, pdo_cnt, pdos))
			charge_manager_update_dualrole(port, CAP_DEDICATED);
		else
			charge_manager_update_dualrole(port, CAP_DUALROLE);
	}
}

/*
 * Evaluate whether our PR role is in the middle of changing, meaning we our
 * current PR role is not the one we expect to have very shortly.
 */
bool pe_is_pr_swapping(int port)
{
	enum usb_pe_state cur_state = get_state_pe(port);

	if (cur_state == PE_PRS_SRC_SNK_EVALUATE_SWAP ||
	    cur_state == PE_PRS_SRC_SNK_TRANSITION_TO_OFF ||
	    cur_state == PE_PRS_SNK_SRC_EVALUATE_SWAP ||
	    cur_state == PE_PRS_SNK_SRC_TRANSITION_TO_OFF)
		return true;

	return false;
}

void pd_request_power_swap(int port)
{
	/* Ignore requests when the board does not wish to swap */
	if (!pd_check_power_swap(port))
		return;

	/* Ignore requests when our power role is transitioning */
	if (pe_is_pr_swapping(port))
		return;

	/*
	 * Always reset the SRC to SNK PR swap counter when a PR swap is
	 * requested by policy.
	 */
	pe[port].src_snk_pr_swap_counter = 0;
}

/**
 * Common sender response message handling
 *
 * This is setup like a pseudo state machine parent state.  It
 * centralizes the SenderResponseTimer for the calling states, as
 * well as checking message send status.
 */
/*
 * pe_sender_response_msg_entry
 * Initialization for handling sender response messages.
 *
 * @param port USB-C Port number
 */
static void pe_sender_response_msg_entry(const int port)
{
	/* Stop sender response timer */
	stop_timer(port, PE_SENDER_RESPONSE);
}

/*
 * pe_sender_response_msg_run
 * Check status of sender response messages.
 *
 * The normal progression of pe_sender_response_msg_entry is:
 *    PENDING -> (COMPLETED/SENT) -> SENT -> SENT ...
 * or
 *    PENDING -> DISCARDED
 *    PENDING -> DPM_DISCARDED
 *
 * NOTE: it is not valid to call this function for a message after
 * receiving either PE_MSG_DISCARDED or PE_MSG_DPM_DISCARDED until
 * another message has been sent and pe_sender_response_msg_entry is called
 * again.
 *
 * @param port USB-C Port number
 * @return the current pe_msg_check
 */
static enum pe_msg_check pe_sender_response_msg_run(const int port)
{
	if (!is_enabled_timer(port, PE_SENDER_RESPONSE)) {
		/* Check for Discard */
		if (PE_CHK_FLAG(port, PE_FLAGS_MSG_DISCARDED)) {
			int dpm_request = pe[port].dpm_curr_request;

			PE_CLR_FLAG(port, PE_FLAGS_MSG_DISCARDED);
			/* Restore the DPM Request */
			if (dpm_request) {
				PE_SET_DPM_REQUEST(port, dpm_request);
				return PE_MSG_DPM_DISCARDED;
			}
			return PE_MSG_DISCARDED;
		}
		/* Check for GoodCRC */
		if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
			PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

			/*
			 * Initialize and run the SenderResponseTimer by
			 * offsetting it with TX transmit success time.
			 * This would remove the effect of the latency from
			 * propagating the TX status.
			 */
			start_timer(port, PE_SENDER_RESPONSE);

			return PE_MSG_SEND_COMPLETED;
		}
		return PE_MSG_SEND_PENDING;
	}
	return PE_MSG_SENT;
}

/*
 * pe_sender_response_msg_exit
 * Exit cleanup for handling sender response messages.
 *
 * @param port USB-C Port number
 */
static void pe_sender_response_msg_exit(int port)
{
	stop_timer(port, PE_SENDER_RESPONSE);
}

/**
 * PE_SRC_Startup
 */
static void pe_src_startup_entry(int port)
{
	print_current_state(port);

	/* Reset CapsCounter */
	pe[port].caps_counter = 0;

	/* Reset the protocol layer */
	prl_reset_soft(port);

	/* Set initial data role */
	pe[port].data_role = pd_get_data_role(port);

	/* Set initial power role */
	pe[port].power_role = PD_ROLE_SOURCE;

	/* Clear explicit contract. */
	pe_invalidate_explicit_contract(port);

	if (PE_CHK_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE);
		/*
		 * Protocol layer reset clears the message IDs for all SOP
		 * types. Indicate that a SOP' soft reset is required before any
		 * other messages are sent to the cable.
		 *
		 * Note that other paths into this state are for the initial
		 * connection and for a hard reset. In both cases the cable
		 * should also automatically clear the message IDs so don't
		 * generate an SOP' soft reset for those cases. Sending
		 * unnecessary SOP' soft resets causes bad behavior with
		 * some devices. See b/179325862.
		 */
		pd_dpm_request(port, DPM_REQUEST_SOP_PRIME_SOFT_RESET_SEND);

		/* Start SwapSourceStartTimer */
		start_timer(port, PE_SWAP_SOURCE_START);
	} else {
		/*
		 * SwapSourceStartTimer delay is not needed, so trigger now.
		 * We can't use set_state_pe here, since we need to ensure that
		 * the protocol layer is running again (done in run function).
		 */
		start_timer(port, PE_SWAP_SOURCE_START);

		pe[port].discover_identity_counter = 0;

		/* Reset dr swap attempt counter */
		pe[port].dr_swap_attempt_counter = 0;
	}
}

static void pe_src_startup_run(int port)
{
	/* Wait until protocol layer is running */
	if (!prl_is_running(port))
		return;

	if (is_expired_timer(port, PE_SWAP_SOURCE_START))
		set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
}

static void pe_src_startup_exit(int port)
{
	stop_timer(port, PE_SWAP_SOURCE_START);
}

/**
 * PE_SRC_Discovery
 */
static void pe_src_discovery_entry(int port)
{
	print_current_state(port);

	/*
	 * Initialize and run the SourceCapabilityTimer in order
	 * to trigger sending a Source_Capabilities Message.
	 *
	 * The SourceCapabilityTimer Shall continue to run during
	 * identity discover and Shall Not be initialized on re-entry
	 * to PE_SRC_Discovery.
	 *
	 * Note: Cable identity is the only valid VDM to probe before a contract
	 * is in place.  All other probing must happen from ready states.
	 */
	start_timer(port, PE_SEND_SOURCE_CAP);
}

static void pe_src_discovery_run(int port)
{

	/*
	 * Transition to the PE_SRC_Send_Capabilities state when:
	 *   1) The SourceCapabilityTimer times out and
	 *      CapsCounter ≤ nCapsCount.
	 *
	 * Transition to the PE_SRC_Disabled state when:
	 *   1) The Port Partners are not presently PD Connected
	 *   2) And the SourceCapabilityTimer times out
	 *   3) And CapsCounter > nCapsCount.
	 *
	 * Transition to the PE_SRC_VDM_Identity_request state when:
	 *   1) DPM requests the identity of the cable plug and
	 *   2) DiscoverIdentityCounter < nDiscoverIdentityCount
	 */
	if (is_expired_timer(port, PE_SEND_SOURCE_CAP)) {
		if (pe[port].caps_counter <= N_CAPS_COUNT) {
			set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
			return;
		} else if (!PE_CHK_FLAG(port, PE_FLAGS_PD_CONNECTION)) {
			set_state_pe(port, PE_SRC_DISABLED);
			return;
		}
	}

	/*
	 * Transition to the PE_SRC_Disabled state when:
	 *   1) The Port Partners have not been PD Connected.
	 *   2) And the NoResponseTimer times out.
	 *   3) And the HardResetCounter > nHardResetCount.
	 */
	if (!PE_CHK_FLAG(port, PE_FLAGS_PD_CONNECTION) &&
	    is_expired_timer(port, PE_NO_RESPONSE) &&
	    pe[port].hard_reset_counter > N_HARD_RESET_COUNT) {
		set_state_pe(port, PE_SRC_DISABLED);
		return;
	}
}

/**
 * PE_SRC_Send_Capabilities
 */
static void pe_src_send_capabilities_entry(int port)
{
	print_current_state(port);

	/* Send PD Capabilities message */
	send_source_cap(port);
	pe_sender_response_msg_entry(port);

	/* Increment CapsCounter */
	pe[port].caps_counter++;
}

static void pe_src_send_capabilities_run(int port)
{
	enum pe_msg_check msg_check;

	/*
	 * Check the state of the message sent
	 */
	msg_check = pe_sender_response_msg_run(port);

	/*
	 * Handle Discarded message
	 *	PE_SNK/SRC_READY if DPM_REQUEST
	 *	PE_SEND_SOFT_RESET otherwise
	 */
	if (msg_check == PE_MSG_DPM_DISCARDED) {
		set_state_pe(port, PE_SRC_READY);
		return;
	} else if (msg_check == PE_MSG_DISCARDED) {
		pe_send_soft_reset(port, TCPCI_MSG_SOP);
		return;
	}

	/*
	 * Handle message that was just sent
	 */
	if (msg_check == PE_MSG_SEND_COMPLETED) {
		/*
		 * If a GoodCRC Message is received then the Policy Engine
		 * Shall:
		 *  1) Stop the NoResponseTimer.
		 *  2) Reset the HardResetCounter and CapsCounter to zero.
		 *  3) Initialize and run the SenderResponseTimer.
		 */
		/* Stop the NoResponseTimer */
		stop_timer(port, PE_NO_RESPONSE);

		/* Reset the HardResetCounter to zero */
		pe[port].hard_reset_counter = 0;

		/* Reset the CapsCounter to zero */
		pe[port].caps_counter = 0;
	}

	/*
	 * Transition to the PE_SRC_Negotiate_Capability state when:
	 *  1) A Request Message is received from the Sink
	 */
	if ((msg_check & PE_MSG_SENT) &&
	    PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		/*
		 * Request Message Received?
		 */
		if (PD_HEADER_CNT(rx_emsg[port].header) > 0 &&
			PD_HEADER_TYPE(rx_emsg[port].header) ==
							PD_DATA_REQUEST) {

			/*
			 * Set to highest revision supported by both
			 * ports.
			 */
			prl_set_rev(port, TCPCI_MSG_SOP,
			MIN(PD_REVISION, PD_HEADER_REV(rx_emsg[port].header)));

			/* We are PD connected */
			PE_SET_FLAG(port, PE_FLAGS_PD_CONNECTION);
			tc_pd_connection(port, 1);
			/*
			 * Handle the Sink Request in
			 * PE_SRC_Negotiate_Capability state
			 */
			set_state_pe(port, PE_SRC_NEGOTIATE_CAPABILITY);
			return;
		}
		/*
		 * We have a Protocol Error.
		 *	PE_SEND_SOFT_RESET
		 */
		pe_send_soft_reset(port,
				   PD_HEADER_GET_SOP(rx_emsg[port].header));
		return;
	}

	/*
	 * Transition to the PE_SRC_Discovery state when:
	 *  1) The Protocol Layer indicates that the Message has not been sent
	 *     and we are presently not Connected
	 *
	 * Send soft reset when:
	 *  1) The Protocol Layer indicates that the Message has not been sent
	 *     and we are already Connected
	 *
	 * See section 8.3.3.4.1.1 PE_SRC_Send_Soft_Reset State and section
	 * 8.3.3.2.3 PE_SRC_Send_Capabilities State.
	 *
	 * NOTE: The PE_FLAGS_PROTOCOL_ERROR is set if a GoodCRC Message
	 *       is not received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
		if (!PE_CHK_FLAG(port, PE_FLAGS_PD_CONNECTION))
			set_state_pe(port, PE_SRC_DISCOVERY);
		else
			pe_send_soft_reset(port, TCPCI_MSG_SOP);

		return;
	}

	/*
	 * Transition to the PE_SRC_Disabled state when:
	 *  1) The Port Partners have not been PD Connected
	 *  2) The NoResponseTimer times out
	 *  3) And the HardResetCounter > nHardResetCount.
	 *
	 * Transition to the Error Recovery state when:
	 *  1) The Port Partners have previously been PD Connected
	 *  2) The NoResponseTimer times out
	 *  3) And the HardResetCounter > nHardResetCount.
	 */
	if (is_expired_timer(port, PE_NO_RESPONSE)) {
		if (pe[port].hard_reset_counter <= N_HARD_RESET_COUNT)
			set_state_pe(port, PE_SRC_HARD_RESET);
		else if (PE_CHK_FLAG(port, PE_FLAGS_PD_CONNECTION))
			set_state_pe(port, PE_WAIT_FOR_ERROR_RECOVERY);
		else
			set_state_pe(port, PE_SRC_DISABLED);
		return;
	}

	/*
	 * Transition to the PE_SRC_Hard_Reset state when:
	 *  1) The SenderResponseTimer times out.
	 */
	if (is_expired_timer(port, PE_SENDER_RESPONSE)) {
		set_state_pe(port, PE_SRC_HARD_RESET);
		return;
	}
}

static void pe_src_send_capabilities_exit(int port)
{
	pe_sender_response_msg_exit(port);
}

/**
 * PE_SRC_Negotiate_Capability
 */
static void pe_src_negotiate_capability_entry(int port)
{
	uint32_t payload;

	print_current_state(port);

	/* Get message payload */
	payload = *(uint32_t *)(&rx_emsg[port].buf);

	/*
	 * Evaluate the Request from the Attached Sink
	 */

	/*
	 * Transition to the PE_SRC_Capability_Response state when:
	 *  1) The Request cannot be met.
	 *  2) Or the Request can be met later from the Power Reserve
	 *
	 * Transition to the PE_SRC_Transition_Supply state when:
	 *  1) The Request can be met
	 *
	 */
	if (pd_check_requested_voltage(payload, port) != EC_SUCCESS) {
		set_state_pe(port, PE_SRC_CAPABILITY_RESPONSE);
	} else {
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		pe[port].requested_idx = RDO_POS(payload);

		set_state_pe(port, PE_SRC_TRANSITION_SUPPLY);
	}
}

/**
 * PE_SRC_Transition_Supply
 */
static void pe_src_transition_supply_entry(int port)
{
	print_current_state(port);

	/* Send a GotoMin Message or otherwise an Accept Message */
	if (PE_CHK_FLAG(port, PE_FLAGS_ACCEPT)) {
		PE_CLR_FLAG(port, PE_FLAGS_ACCEPT);
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_ACCEPT);
	} else {
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_GOTO_MIN);
	}
}

static void pe_src_transition_supply_run(int port)
{
	/*
	 * Transition to the PE_SRC_Ready state when:
	 *  1) The power supply is ready.
	 *
	 *  NOTE: This code block is executed twice:
	 *        First Pass)
	 *            When PE_FLAGS_TX_COMPLETE is set due to the
	 *            PD_CTRL_ACCEPT or PD_CTRL_GOTO_MIN messages
	 *            being sent.
	 *
	 *        Second Pass)
	 *            When PE_FLAGS_TX_COMPLETE is set due to the
	 *            PD_CTRL_PS_RDY message being sent.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		/*
		 * NOTE: If a message was received,
		 * pe_src_ready state will handle it.
		 */

		if (PE_CHK_FLAG(port, PE_FLAGS_PS_READY)) {
			PE_CLR_FLAG(port, PE_FLAGS_PS_READY);

			/* NOTE: Second pass through this code block */
			/* Explicit Contract is now in place */
			pe_set_explicit_contract(port);

			/*
			 * Setup to get Device Policy Manager to request
			 * Source Capabilities, if needed, for possible
			 * PR_Swap.  Get the number directly to avoid re-probing
			 * if the partner generated an error and left -1 for the
			 * count.
			 */
			if (pe[port].src_cap_cnt == 0)
				pd_dpm_request(port, DPM_REQUEST_GET_SRC_CAPS);
			set_state_pe(port, PE_SRC_READY);
		} else {
			/* NOTE: First pass through this code block */
			/* Wait for tSrcTransition before changing supply. */
			/*
			 *
			 */
			pd_transition_voltage(pe[port].requested_idx);
			send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_PS_RDY);
			PE_SET_FLAG(port, PE_FLAGS_PS_READY);
		}

		return;
	}

	/*
	 * Transition to the PE_SRC_Hard_Reset state when:
	 *  1) A Protocol Error occurs.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
		set_state_pe(port, PE_SRC_HARD_RESET);
	}
}

static void pe_src_transition_supply_exit(int port)
{
	stop_timer(port, PE_PS_TRANSITION);
}

/*
 * Transitions state after receiving a Not Supported extended message. Under
 * appropriate conditions, transitions to a PE_{SRC,SNK}_Chunk_Received.
 */
static void extended_message_not_supported(int port, uint32_t *payload)
{
	set_state_pe(port, PE_SEND_NOT_SUPPORTED);
}

/**
 * PE_SRC_Ready
 */
static void pe_src_ready_entry(int port)
{
	print_current_state(port);

	/* Ensure any message send flags are cleaned up */
	PE_CLR_FLAG(port, PE_FLAGS_READY_CLR);

	/* Clear DPM Current Request */
	pe[port].dpm_curr_request = 0;
}

static void pe_src_ready_run(int port)
{
	/*
	 * Handle incoming messages before discovery and DPMs other than hard
	 * reset
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		uint8_t type = PD_HEADER_TYPE(rx_emsg[port].header);
		uint8_t cnt = PD_HEADER_CNT(rx_emsg[port].header);
		uint8_t ext = PD_HEADER_EXT(rx_emsg[port].header);
		uint32_t *payload = (uint32_t *)rx_emsg[port].buf;

		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		/* Extended Message Requests */
		if (ext > 0) {
			extended_message_not_supported(port, payload);
			return;
		}
		/* Data Message Requests */
		else if (cnt > 0) {
			switch (type) {
			case PD_DATA_REQUEST:
				set_state_pe(port, PE_SRC_NEGOTIATE_CAPABILITY);
				return;
			case PD_DATA_SINK_CAP:
				break;
			case PD_DATA_VENDOR_DEF:
				if (PD_HEADER_TYPE(rx_emsg[port].header) ==
							PD_DATA_VENDOR_DEF) {
					if (PD_VDO_SVDM(*payload)) {
						set_state_pe(port,
							PE_VDM_RESPONSE);
					}
				}
				return;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
				return;
			}
		}
		/* Control Message Requests */
		else {
			switch (type) {
			case PD_CTRL_GOOD_CRC:
				break;
			case PD_CTRL_NOT_SUPPORTED:
				break;
			case PD_CTRL_PING:
				break;
			case PD_CTRL_GET_SOURCE_CAP:
				set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
				return;
			case PD_CTRL_GET_SINK_CAP:
				set_state_pe(port, PE_SNK_GIVE_SINK_CAP);
				return;
			case PD_CTRL_GOTO_MIN:
				break;
			case PD_CTRL_PR_SWAP:
				set_state_pe(port,
					PE_PRS_SRC_SNK_EVALUATE_SWAP);
				return;
			case PD_CTRL_DR_SWAP:
				set_state_pe(port, PE_DRS_EVALUATE_SWAP);
				return;
			case PD_CTRL_VCONN_SWAP:
				if (IS_ENABLED(CONFIG_USBC_VCONN))
					set_state_pe(port,
							PE_VCS_EVALUATE_SWAP);
				else
					set_state_pe(port,
							PE_SEND_NOT_SUPPORTED);
				return;
			/*
			 * USB PD 3.0 6.8.1:
			 * Receiving an unexpected message shall be responded
			 * to with a soft reset message.
			 */
			case PD_CTRL_ACCEPT:
			case PD_CTRL_REJECT:
			case PD_CTRL_WAIT:
			case PD_CTRL_PS_RDY:
				pe_send_soft_reset(port,
				  PD_HEADER_GET_SOP(rx_emsg[port].header));
				return;
			/*
			 * Receiving an unknown or unsupported message
			 * shall be responded to with a not supported message.
			 */
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
				return;
			}
		}
	}

	/*
	 * Make sure the PRL layer isn't busy with receiving or transmitting
	 * chunked messages before attempting to transmit a new message.
	 */
	if (prl_is_busy(port))
		return;

	if (PE_CHK_FLAG(port, PE_FLAGS_WAITING_PR_SWAP) &&
	     is_expired_timer(port, PE_PR_SWAP_WAIT)) {
		PE_CLR_FLAG(port, PE_FLAGS_WAITING_PR_SWAP);
		PE_SET_DPM_REQUEST(port, DPM_REQUEST_PR_SWAP);
	}

	/*
	 * Handle Device Policy Manager Requests
	 */
	if (source_dpm_requests(port))
		return;
}

/**
 * PE_SRC_Disabled
 */
static void pe_src_disabled_entry(int port)
{
	print_current_state(port);

	/*
	 * Unresponsive to USB Power Delivery messaging, but not to Hard Reset
	 * Signaling. See pe_got_hard_reset
	 */
}

/**
 * PE_SRC_Capability_Response
 */
static void pe_src_capability_response_entry(int port)
{
	print_current_state(port);

	/* NOTE: Wait messaging should be implemented. */

	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_REJECT);
}

static void pe_src_capability_response_run(int port)
{
	/*
	 * Transition to the PE_SRC_Ready state when:
	 *  1) There is an Explicit Contract and
	 *  2) A Reject Message has been sent and the present Contract is still
	 *     Valid or
	 *  3) A Wait Message has been sent.
	 *
	 * Transition to the PE_SRC_Hard_Reset state when:
	 *  1) There is an Explicit Contract and
	 *  2) The Reject Message has been sent and the present
	 *     Contract is Invalid
	 *
	 * Transition to the PE_SRC_Wait_New_Capabilities state when:
	 *  1) There is no Explicit Contract and
	 *  2) A Reject Message has been sent or
	 *  3) A Wait Message has been sent.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		if (PE_CHK_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT))
			/*
			 * NOTE: The src capabilities listed in
			 *       board/xxx/usb_pd_policy.c will not
			 *       change so the present contract will
			 *       never be invalid.
			 */
			set_state_pe(port, PE_SRC_READY);
		else
			/*
			 * NOTE: The src capabilities listed in
			 *       board/xxx/usb_pd_policy.c will not
			 *       change, so no need to resending them
			 *       again. Transition to disabled state.
			 */
			set_state_pe(port, PE_SRC_DISABLED);
	}
}

/**
 * PE_SRC_Hard_Reset
 */
static void pe_src_hard_reset_entry(int port)
{
	print_current_state(port);

	/* Generate Hard Reset Signal */
	prl_execute_hard_reset(port);

	/* Increment the HardResetCounter */
	pe[port].hard_reset_counter++;

	/* Start NoResponseTimer */
	start_timer(port, PE_NO_RESPONSE);

	/* Start PSHardResetTimer */
	start_timer(port, PE_PS_HARD_RESET);

	/* Clear error flags */
	PE_CLR_FLAG(port, PE_FLAGS_VDM_REQUEST_NAKED |
			  PE_FLAGS_PROTOCOL_ERROR |
			  PE_FLAGS_VDM_REQUEST_BUSY);
}

static void pe_src_hard_reset_run(int port)
{
	/*
	 * Transition to the PE_SRC_Transition_to_default state when:
	 *  1) The PSHardResetTimer times out.
	 */
	if (is_expired_timer(port, PE_PS_HARD_RESET))
		set_state_pe(port, PE_SRC_TRANSITION_TO_DEFAULT);
}

static void pe_src_hard_reset_exit(int port)
{
	stop_timer(port, PE_PS_HARD_RESET);
}

/**
 * PE_SRC_Hard_Reset_Received
 */
static void pe_src_hard_reset_received_entry(int port)
{
	print_current_state(port);

	/* Start NoResponseTimer */
	start_timer(port, PE_NO_RESPONSE);

	/* Start PSHardResetTimer */
	start_timer(port, PE_PS_HARD_RESET);
}

static void pe_src_hard_reset_received_run(int port)
{
	/*
	 * Transition to the PE_SRC_Transition_to_default state when:
	 *  1) The PSHardResetTimer times out.
	 */
	if (is_expired_timer(port, PE_PS_HARD_RESET))
		set_state_pe(port, PE_SRC_TRANSITION_TO_DEFAULT);
}

static void pe_src_hard_reset_received_exit(int port)
{
	stop_timer(port, PE_PS_HARD_RESET);
}

/**
 * PE_SRC_Transition_To_Default
 */
static void pe_src_transition_to_default_entry(int port)
{
	print_current_state(port);

	/* Reset flags */
	pe[port].flags = 0;

	/* Reset DPM Request */
	pe[port].dpm_request = 0;

	/*
	 * Request Device Policy Manager to request power
	 * supply Hard Resets to vSafe5V via vSafe0V
	 * Reset local HW
	 * Request Device Policy Manager to set Port Data
	 * Role to DFP and turn off VCONN
	 */
	tc_hard_reset_request(port);
}

static void pe_src_transition_to_default_run(int port)
{
	/*
	 * Transition to the PE_SRC_Startup state when:
	 *   1) The power supply has reached the default level.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PS_RESET_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_PS_RESET_COMPLETE);
		/* Inform the Protocol Layer that the Hard Reset is complete */
		prl_hard_reset_complete(port);
		set_state_pe(port, PE_SRC_STARTUP);
	}
}

/**
 * PE_SNK_Startup State
 */
static void pe_snk_startup_entry(int port)
{
	print_current_state(port);

	/* Reset the protocol layer */
	prl_reset_soft(port);

	/* Set initial data role */
	pe[port].data_role = pd_get_data_role(port);

	/* Set initial power role */
	pe[port].power_role = PD_ROLE_SINK;

	/* Invalidate explicit contract */
	pe_invalidate_explicit_contract(port);

	if (PE_CHK_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE);
		/*
		 * Protocol layer reset clears the message IDs for all SOP
		 * types. Indicate that a SOP' soft reset is required before any
		 * other messages are sent to the cable.
		 *
		 * Note that other paths into this state are for the initial
		 * connection and for a hard reset. In both cases the cable
		 * should also automatically clear the message IDs so don't
		 * generate an SOP' soft reset for those cases. Sending
		 * unnecessary SOP' soft resets causes bad behavior with
		 * some devices. See b/179325862.
		 */
		pd_dpm_request(port, DPM_REQUEST_SOP_PRIME_SOFT_RESET_SEND);
	} else {
		pe[port].discover_identity_counter = 0;

		/* Reset dr swap attempt counter */
		pe[port].dr_swap_attempt_counter = 0;

		charge_manager_force_ceil(port, PD_MIN_MA);
	}
}

static void pe_snk_startup_run(int port)
{
	/* Wait until protocol layer is running */
	if (!prl_is_running(port))
		return;

	/*
	 * Once the reset process completes, the Policy Engine Shall
	 * transition to the PE_SNK_Discovery state
	 */
	set_state_pe(port, PE_SNK_DISCOVERY);
}

/**
 * PE_SNK_Discovery State
 */
static void pe_snk_discovery_entry(int port)
{
	print_current_state(port);

	charge_manager_update_dualrole(port, CAP_DEDICATED);
}

static void pe_snk_discovery_run(int port)
{
	/*
	 * Transition to the PE_SNK_Wait_for_Capabilities state when:
	 *   1) VBUS has been detected
	 */
	if (!pd_check_vbus_level(port, VBUS_REMOVED))
		set_state_pe(port, PE_SNK_WAIT_FOR_CAPABILITIES);
}

/**
 * PE_SNK_Wait_For_Capabilities State
 */
static void pe_snk_wait_for_capabilities_entry(int port)
{
	print_current_state(port);

	/* Initialize and start the SinkWaitCapTimer */
	start_timer(port, PE_SINK_WAIT_CAP);
}

static void pe_snk_wait_for_capabilities_run(int port)
{
	uint8_t type;
	uint8_t cnt;
	uint8_t ext;

	/*
	 * Transition to the PE_SNK_Evaluate_Capability state when:
	 *  1) A Source_Capabilities Message is received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt > 0) && (type == PD_DATA_SOURCE_CAP)) {
			set_state_pe(port, PE_SNK_EVALUATE_CAPABILITY);
			return;
		}
	}

	/* When the SinkWaitCapTimer times out, perform a Hard Reset. */
	if (is_expired_timer(port, PE_SINK_WAIT_CAP)) {
		PE_SET_FLAG(port, PE_FLAGS_SNK_WAIT_CAP_TIMEOUT);
		set_state_pe(port, PE_SNK_HARD_RESET);
	}
}

static void pe_snk_wait_for_capabilities_exit(int port)
{
	stop_timer(port, PE_SINK_WAIT_CAP);
}

/**
 * PE_SNK_Evaluate_Capability State
 */
static void pe_snk_evaluate_capability_entry(int port)
{
	uint32_t *pdo = (uint32_t *)rx_emsg[port].buf;
	uint32_t num = rx_emsg[port].len >> 2;

	print_current_state(port);

	/* Reset Hard Reset counter to zero */
	pe[port].hard_reset_counter = 0;

	/* Set to highest revision supported by both ports. */
	prl_set_rev(port, TCPCI_MSG_SOP,
			MIN(PD_REVISION, PD_HEADER_REV(rx_emsg[port].header)));

	/* Parse source caps if they have changed */
	if (pe[port].src_cap_cnt != num ||
	    memcmp(pdo, pe[port].src_caps, num << 2)) {
		pe[port].src_cap_cnt = num;
		/*
		 * If port policy preference is to be a power role source,
		 * then request a power role swap.  If we'd previously queued a
		 * PR swap but can now charge from this device, clear it.
		 */
		if (!pd_can_charge_from_device(port, num, pdo))
			pd_request_power_swap(port);
		else
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_PR_SWAP);
	}

	pe_update_src_pdo_flags(port, num, pdo);

	pd_set_src_caps(port, num, pdo);
	/* Evaluate the options based on supplied capabilities */
	pd_process_source_cap(port, pe[port].src_cap_cnt, pe[port].src_caps);
	/* Device Policy Response Received */
	set_state_pe(port, PE_SNK_SELECT_CAPABILITY);
}

/**
 * PE_SNK_Select_Capability State
 */
static void pe_snk_select_capability_entry(int port)
{
	print_current_state(port);

	charge_manager_force_ceil(port, PD_MIN_MA);

	/* Send Request */
	pe_send_request_msg(port);
	pe_sender_response_msg_entry(port);

	/* We are PD Connected */
	PE_SET_FLAG(port, PE_FLAGS_PD_CONNECTION);
	tc_pd_connection(port, 1);
}

static void pe_snk_select_capability_run(int port)
{
	uint8_t type;
	uint8_t cnt;
	enum tcpci_msg_type sop;
	enum pe_msg_check msg_check;

	/*
	 * Check the state of the message sent
	 */
	msg_check = pe_sender_response_msg_run(port);

	/*
	 * Handle discarded message
	 */
	if (msg_check & PE_MSG_DISCARDED) {
		/*
		 * The sent REQUEST message was discarded.  This can be at
		 * the start of an AMS or in the middle.  Handle what to
		 * do based on where we came from.
		 * 1) SE_SNK_EVALUATE_CAPABILITY: sends SoftReset
		 * 2) SE_SNK_READY: goes back to SNK Ready
		 */
		if (get_last_state_pe(port) == PE_SNK_EVALUATE_CAPABILITY)
			pe_send_soft_reset(port, TCPCI_MSG_SOP);
		else
			set_state_pe(port, PE_SNK_READY);
		return;
	}

	if ((msg_check & PE_MSG_SENT) &&
	    PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);
		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		sop = PD_HEADER_GET_SOP(rx_emsg[port].header);

		/*
		 * Transition to the PE_SNK_Transition_Sink state when:
		 *  1) An Accept Message is received from the Source.
		 *
		 * Transition to the PE_SNK_Wait_for_Capabilities state when:
		 *  1) There is no Explicit Contract in place and
		 *  2) A Reject Message is received from the Source or
		 *  3) A Wait Message is received from the Source.
		 *
		 * Transition to the PE_SNK_Ready state when:
		 *  1) There is an Explicit Contract in place and
		 *  2) A Reject Message is received from the Source or
		 *  3) A Wait Message is received from the Source.
		 *
		 * Transition to the PE_SNK_Hard_Reset state when:
		 *  1) A SenderResponseTimer timeout occurs.
		 */

		/* Only look at control messages */
		if (cnt == 0) {
			/*
			 * Accept Message Received
			 */
			if (type == PD_CTRL_ACCEPT) {
				/* explicit contract is now in place */
				pe_set_explicit_contract(port);

				set_state_pe(port, PE_SNK_TRANSITION_SINK);

				return;
			}
			/*
			 * Reject or Wait Message Received
			 */
			else if (type == PD_CTRL_REJECT ||
							type == PD_CTRL_WAIT) {
				if (type == PD_CTRL_WAIT)
					PE_SET_FLAG(port, PE_FLAGS_WAIT);

				stop_timer(port, PE_SINK_REQUEST);

				/*
				 * We had a previous explicit contract, so
				 * transition to PE_SNK_Ready
				 */
				if (PE_CHK_FLAG(port,
						PE_FLAGS_EXPLICIT_CONTRACT))
					set_state_pe(port, PE_SNK_READY);
				/*
				 * No previous explicit contract, so transition
				 * to PE_SNK_Wait_For_Capabilities
				 */
				else
					set_state_pe(port,
						PE_SNK_WAIT_FOR_CAPABILITIES);
				return;
			}
			/*
			 * Unexpected Control Message Received
			 */
			else {
				/* Send Soft Reset */
				pe_send_soft_reset(port, sop);
				return;
			}
		}
		/*
		 * Unexpected Data Message
		 */
		else {
			/* Send Soft Reset */
			pe_send_soft_reset(port, sop);
			return;
		}
	}

	/* SenderResponsetimer timeout */
	if (is_expired_timer(port, PE_SENDER_RESPONSE))
		set_state_pe(port, PE_SNK_HARD_RESET);
}

void pe_snk_select_capability_exit(int port)
{
	pe_sender_response_msg_exit(port);
}

/**
 * PE_SNK_Transition_Sink State
 */
static void pe_snk_transition_sink_entry(int port)
{
	print_current_state(port);

	/* Initialize and run PSTransitionTimer */
	start_timer(port, PE_PS_TRANSITION);
}

static void pe_snk_transition_sink_run(int port)
{
	/*
	 * Transition to the PE_SNK_Ready state when:
	 *  1) A PS_RDY Message is received from the Source.
	 *
	 * Transition to the PE_SNK_Hard_Reset state when:
	 *  1) A Protocol Error occurs.
	 */

	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		/*
		 * PS_RDY message received
		 */
		if ((PD_HEADER_CNT(rx_emsg[port].header) == 0) &&
			   (PD_HEADER_TYPE(rx_emsg[port].header) ==
			   PD_CTRL_PS_RDY)) {
			/* Tell the DUT port that a new voltage is ready */
			set_state_pe(port, PE_SNK_READY);
		} else {
			/*
			 * Protocol Error
			 */
			set_state_pe(port, PE_SNK_HARD_RESET);
		}
		return;
	}

	/*
	 * Timeout will lead to a Hard Reset
	 */
	if (is_expired_timer(port, PE_PS_TRANSITION) &&
		pe[port].hard_reset_counter <= N_HARD_RESET_COUNT) {
		PE_SET_FLAG(port, PE_FLAGS_PS_TRANSITION_TIMEOUT);

		set_state_pe(port, PE_SNK_HARD_RESET);
	}
}

static void pe_snk_transition_sink_exit(int port)
{
	/* Transition Sink's power supply to the new power level */
	pd_set_input_current_limit(port,
				pe[port].curr_limit, pe[port].supply_voltage);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		/* Set ceiling based on what's negotiated */
		charge_manager_set_ceil(port,
				CEIL_REQUESTOR_PD, pe[port].curr_limit);

	stop_timer(port, PE_PS_TRANSITION);
}


/**
 * PE_SNK_Ready State
 */
static void pe_snk_ready_entry(int port)
{
	print_current_state(port);

	/* Ensure any message send flags are cleaned up */
	PE_CLR_FLAG(port, PE_FLAGS_READY_CLR);

	/* Clear DPM Current Request */
	pe[port].dpm_curr_request = 0;

	/*
	 * On entry to the PE_SNK_Ready state as the result of a wait,
	 * then do the following:
	 *   1) Initialize and run the SinkRequestTimer
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_WAIT)) {
		PE_CLR_FLAG(port, PE_FLAGS_WAIT);
		start_timer(port, PE_SINK_REQUEST);
	}
}

static void pe_snk_ready_run(int port)
{
	/*
	 * Handle incoming messages before discovery and DPMs other than hard
	 * reset
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		uint8_t type = PD_HEADER_TYPE(rx_emsg[port].header);
		uint8_t cnt = PD_HEADER_CNT(rx_emsg[port].header);
		uint8_t ext = PD_HEADER_EXT(rx_emsg[port].header);
		uint32_t *payload = (uint32_t *)rx_emsg[port].buf;

		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		/* Extended Message Request */
		if (ext > 0) {
			extended_message_not_supported(port, payload);
			return;
		}
		/* Data Messages */
		else if (cnt > 0) {
			switch (type) {
			case PD_DATA_SOURCE_CAP:
				set_state_pe(port,
					PE_SNK_EVALUATE_CAPABILITY);
				break;
			case PD_DATA_VENDOR_DEF:
				if (PD_HEADER_TYPE(rx_emsg[port].header) ==
							PD_DATA_VENDOR_DEF) {
					if (PD_VDO_SVDM(*payload))
						set_state_pe(port,
							PE_VDM_RESPONSE);
				}
				break;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
			}
			return;
		}
		/* Control Messages */
		else {
			switch (type) {
			case PD_CTRL_GOOD_CRC:
				/* Do nothing */
				break;
			case PD_CTRL_PING:
				/* Do nothing */
				break;
			case PD_CTRL_GET_SOURCE_CAP:
				set_state_pe(port, PE_DR_SNK_GIVE_SOURCE_CAP);
				return;
			case PD_CTRL_GET_SINK_CAP:
				set_state_pe(port, PE_SNK_GIVE_SINK_CAP);
				return;
			case PD_CTRL_GOTO_MIN:
				set_state_pe(port, PE_SNK_TRANSITION_SINK);
				return;
			case PD_CTRL_PR_SWAP:
				set_state_pe(port,
						PE_PRS_SNK_SRC_EVALUATE_SWAP);
				return;
			case PD_CTRL_DR_SWAP:
				if (PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION))
					set_state_pe(port, PE_SNK_HARD_RESET);
				else
					set_state_pe(port,
							PE_DRS_EVALUATE_SWAP);
				return;
			case PD_CTRL_VCONN_SWAP:
				if (IS_ENABLED(CONFIG_USBC_VCONN))
					set_state_pe(port,
							PE_VCS_EVALUATE_SWAP);
				else
					set_state_pe(port,
							PE_SEND_NOT_SUPPORTED);
				return;
			case PD_CTRL_NOT_SUPPORTED:
				/* Do nothing */
				break;
			/*
			 * USB PD 3.0 6.8.1:
			 * Receiving an unexpected message shall be responded
			 * to with a soft reset message.
			 */
			case PD_CTRL_ACCEPT:
			case PD_CTRL_REJECT:
			case PD_CTRL_WAIT:
			case PD_CTRL_PS_RDY:
				pe_send_soft_reset(port,
				  PD_HEADER_GET_SOP(rx_emsg[port].header));
				return;
			/*
			 * Receiving an unknown or unsupported message
			 * shall be responded to with a not supported message.
			 */
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
				return;
			}
		}
	}

	/*
	 * Make sure the PRL layer isn't busy with receiving or transmitting
	 * chunked messages before attempting to transmit a new message.
	 */
	if (prl_is_busy(port))
		return;

	if (is_expired_timer(port, PE_SINK_REQUEST)) {
		stop_timer(port, PE_SINK_REQUEST);
		set_state_pe(port, PE_SNK_SELECT_CAPABILITY);
		return;
	}

	/*
	 * Handle Device Policy Manager Requests
	 */
	if (sink_dpm_requests(port))
		return;
}

/**
 * PE_SNK_Hard_Reset
 */
static void pe_snk_hard_reset_entry(int port)
{
	print_current_state(port);

	/*
	 * Note: If the SinkWaitCapTimer times out and the HardResetCounter is
	 *       greater than nHardResetCount the Sink Shall assume that the
	 *       Source is non-responsive.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_SNK_WAIT_CAP_TIMEOUT) &&
			pe[port].hard_reset_counter > N_HARD_RESET_COUNT) {
		set_state_pe(port, PE_SRC_DISABLED);
		return;
	}

	PE_CLR_FLAG(port, PE_FLAGS_SNK_WAIT_CAP_TIMEOUT |
			  PE_FLAGS_VDM_REQUEST_NAKED |
			  PE_FLAGS_PROTOCOL_ERROR |
			  PE_FLAGS_VDM_REQUEST_BUSY);

	/* Request the generation of Hard Reset Signaling by the PHY Layer */
	prl_execute_hard_reset(port);

	/* Increment the HardResetCounter */
	pe[port].hard_reset_counter++;

	/*
	 * Transition the Sink’s power supply to the new power level if
	 * PSTransitionTimer timeout occurred.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PS_TRANSITION_TIMEOUT)) {
		PE_CLR_FLAG(port, PE_FLAGS_PS_TRANSITION_TIMEOUT);

		/* Transition Sink's power supply to the new power level */
		pd_set_input_current_limit(port, pe[port].curr_limit,
						pe[port].supply_voltage);
		if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
			/* Set ceiling based on what's negotiated */
			charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
							pe[port].curr_limit);
	}
}

static void pe_snk_hard_reset_run(int port)
{
	/*
	 * Transition to the PE_SNK_Transition_to_default state when:
	 *  1) The Hard Reset is complete.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_HARD_RESET_PENDING))
		return;

	set_state_pe(port, PE_SNK_TRANSITION_TO_DEFAULT);
}

/**
 * PE_SNK_Transition_to_default
 */
static void pe_snk_transition_to_default_entry(int port)
{
	print_current_state(port);

	/* Reset flags */
	pe[port].flags = 0;

	/* Reset DPM Request */
	pe[port].dpm_request = 0;

	/* Inform the TC Layer of Hard Reset */
	tc_hard_reset_request(port);
}

static void pe_snk_transition_to_default_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_PS_RESET_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_PS_RESET_COMPLETE);
		/* Inform the Protocol Layer that the Hard Reset is complete */
		prl_hard_reset_complete(port);
		set_state_pe(port, PE_SNK_STARTUP);
	}
}

/**
 * PE_SNK_Get_Source_Cap
 */
static void pe_snk_get_source_cap_entry(int port)
{
	print_current_state(port);

	/* Send a Get_Source_Cap Message */
	tx_emsg[port].len = 0;
	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_GET_SOURCE_CAP);
}

static void pe_snk_get_source_cap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		set_state_pe(port, PE_SNK_READY);
	}
}

/**
 * PE_SNK_Send_Soft_Reset and PE_SRC_Send_Soft_Reset
 */
static void pe_send_soft_reset_entry(int port)
{
	print_current_state(port);

	/* Reset Protocol Layer (softly) */
	prl_reset_soft(port);

	pe_sender_response_msg_entry(port);

	/*
	 * Mark the temporary timer PE_TIMER_TIMEOUT as expired to limit
	 * to sending a single SoftReset message.
	 */
	PE_SET_FLAG(port, PE_FLAGS_SEND_SOFT_RESET_ONCE);
}

static void pe_send_soft_reset_run(int port)
{
	int type;
	int cnt;
	int ext;
	enum pe_msg_check msg_check;

	/* Wait until protocol layer is running */
	if (!prl_is_running(port))
		return;

	/*
	 * Protocol layer is running, so need to send a single SoftReset.
	 * Use temporary timer to act as a flag to keep this as a single
	 * message send.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_SEND_SOFT_RESET_ONCE)) {
		PE_CLR_FLAG(port, PE_FLAGS_SEND_SOFT_RESET_ONCE);

		/*
		 * TODO(b/150614211): Soft reset type should match
		 * unexpected incoming message type
		 */
		/* Send Soft Reset message */
		send_ctrl_msg(port,
			pe[port].soft_reset_sop, PD_CTRL_SOFT_RESET);

		return;
	}

	/*
	 * Check the state of the message sent
	 */
	msg_check = pe_sender_response_msg_run(port);

	/*
	 * Handle discarded message
	 */
	if (msg_check == PE_MSG_DISCARDED) {
		pe_set_ready_state(port);
		return;
	}

	/*
	 * Transition to the PE_SNK_Send_Capabilities or
	 * PE_SRC_Send_Capabilities state when:
	 *   1) An Accept Message has been received.
	 */
	if (msg_check == PE_MSG_SENT &&
	    PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0) && (type == PD_CTRL_ACCEPT)) {
			if (pe[port].power_role == PD_ROLE_SINK)
				set_state_pe(port,
						PE_SNK_WAIT_FOR_CAPABILITIES);
			else
				set_state_pe(port,
						PE_SRC_SEND_CAPABILITIES);
			return;
		}
	}

	/*
	 * Transition to PE_SNK_Hard_Reset or PE_SRC_Hard_Reset on Sender
	 * Response Timer Timeout or Protocol Layer or Protocol Error
	 */
	if (is_expired_timer(port, PE_SENDER_RESPONSE) ||
			PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);

		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SNK_HARD_RESET);
		else
			set_state_pe(port, PE_SRC_HARD_RESET);
		return;
	}
}

static void pe_send_soft_reset_exit(int port)
{
	pe_sender_response_msg_exit(port);
	PE_CLR_FLAG(port, PE_FLAGS_SEND_SOFT_RESET_ONCE);
}

/**
 * PE_SNK_Soft_Reset and PE_SNK_Soft_Reset
 */
static void pe_soft_reset_entry(int port)
{
	print_current_state(port);

	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_ACCEPT);
}

static void  pe_soft_reset_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SNK_WAIT_FOR_CAPABILITIES);
		else
			set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
	} else if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);

		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SNK_HARD_RESET);
		else
			set_state_pe(port, PE_SRC_HARD_RESET);
	}
}

/**
 * PE_SRC_Not_Supported and PE_SNK_Not_Supported
 *
 * 6.7.1 Soft Reset and Protocol Error (Revision 2.0, Version 1.3)
 * An unrecognized or unsupported Message (except for a Structured VDM),
 * received in the PE_SNK_Ready or PE_SRC_Ready states, Shall Not cause
 * a Soft_Reset Message to be generated but instead a Reject Message
 * Shall be generated.
 */
static void pe_send_not_supported_entry(int port)
{
	print_current_state(port);

	/* Request the Protocol Layer to send a Not_Supported Message. */
	if (prl_get_rev(port, TCPCI_MSG_SOP) > PD_REV20)
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_NOT_SUPPORTED);
	else
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_REJECT);
}

static void pe_send_not_supported_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		pe_set_ready_state(port);

	}
}

/**
 * PE_SRC_Ping
 */
static void pe_src_ping_entry(int port)
{
	print_current_state(port);
	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_PING);
}

static void pe_src_ping_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		set_state_pe(port, PE_SRC_READY);
	}
}

/**
 * PE_DRS_Evaluate_Swap
 */
static void pe_drs_evaluate_swap_entry(int port)
{
	print_current_state(port);

	/* Get evaluation of Data Role Swap request from DPM */
	if (pd_check_data_swap(port, pe[port].data_role)) {
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		/*
		 * PE_DRS_UFP_DFP_Evaluate_Swap and
		 * PE_DRS_DFP_UFP_Evaluate_Swap states embedded here.
		 */
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_ACCEPT);
	} else {
		/*
		 * PE_DRS_UFP_DFP_Reject_Swap and PE_DRS_DFP_UFP_Reject_Swap
		 * states embedded here.
		 */
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_REJECT);
	}
}

static void pe_drs_evaluate_swap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/* Accept Message sent. Transition to PE_DRS_Change */
		if (PE_CHK_FLAG(port, PE_FLAGS_ACCEPT)) {
			PE_CLR_FLAG(port, PE_FLAGS_ACCEPT);
			set_state_pe(port, PE_DRS_CHANGE);
		} else {
			/*
			 * Message sent. Transition back to PE_SRC_Ready or
			 * PE_SNK_Ready.
			 */
			pe_set_ready_state(port);
		}
	}
}

/**
 * PE_DRS_Change
 */
static void pe_drs_change_entry(int port)
{
	print_current_state(port);

	/*
	 * PE_DRS_UFP_DFP_Change_to_DFP and PE_DRS_DFP_UFP_Change_to_UFP
	 * states embedded here.
	 */
	/* Request DPM to change port data role */
	pd_request_data_swap(port);
}

static void pe_drs_change_run(int port)
{
	/* Wait until the data role is changed */
	if (pe[port].data_role == pd_get_data_role(port))
		return;

	/* Update the data role */
	pe[port].data_role = pd_get_data_role(port);

	if (pe[port].data_role == PD_ROLE_DFP)
		PE_CLR_FLAG(port, PE_FLAGS_DR_SWAP_TO_DFP);

	/*
	 * Port changed. Transition back to PE_SRC_Ready or
	 * PE_SNK_Ready.
	 */
	pe_set_ready_state(port);
}

/**
 * PE_DRS_Send_Swap
 */
static void pe_drs_send_swap_entry(int port)
{
	print_current_state(port);

	/*
	 * PE_DRS_UFP_DFP_Send_Swap and PE_DRS_DFP_UFP_Send_Swap
	 * states embedded here.
	 */
	/* Request the Protocol Layer to send a DR_Swap Message */
	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_DR_SWAP);
	pe_sender_response_msg_entry(port);
}

static void pe_drs_send_swap_run(int port)
{
	int type;
	int cnt;
	int ext;
	enum pe_msg_check msg_check;

	/*
	 * Check the state of the message sent
	 */
	msg_check = pe_sender_response_msg_run(port);

	/*
	 * Transition to PE_DRS_Change when:
	 *   1) An Accept Message is received.
	 *
	 * Transition to PE_SRC_Ready or PE_SNK_Ready state when:
	 *   1) A Reject Message is received.
	 *   2) Or a Wait Message is received.
	 */
	if ((msg_check & PE_MSG_SENT) &&
	    PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0)) {
			if (type == PD_CTRL_ACCEPT) {
				set_state_pe(port, PE_DRS_CHANGE);
				return;
			} else if ((type == PD_CTRL_REJECT) ||
					(type == PD_CTRL_WAIT) ||
					(type == PD_CTRL_NOT_SUPPORTED)) {
				pe_set_ready_state(port);
				return;
			}
		}
	}

	/*
	 * Transition to PE_SRC_Ready or PE_SNK_Ready state when:
	 *   1) the SenderResponseTimer times out.
	 *   2) Message was discarded.
	 */
	if ((msg_check & PE_MSG_DISCARDED) ||
	    is_expired_timer(port, PE_SENDER_RESPONSE))
		pe_set_ready_state(port);
}

static void pe_drs_send_swap_exit(int port)
{
	pe_sender_response_msg_exit(port);
}

/**
 * PE_PRS_SRC_SNK_Evaluate_Swap
 */
static void pe_prs_src_snk_evaluate_swap_entry(int port)
{
	print_current_state(port);

	if (!pd_check_power_swap(port)) {
		/* PE_PRS_SRC_SNK_Reject_PR_Swap state embedded here */
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_REJECT);
	} else {
		tc_request_power_swap(port);
		/* PE_PRS_SRC_SNK_Accept_Swap state embedded here */
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_ACCEPT);
	}
}

static void pe_prs_src_snk_evaluate_swap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		if (PE_CHK_FLAG(port, PE_FLAGS_ACCEPT)) {
			PE_CLR_FLAG(port, PE_FLAGS_ACCEPT);

			/*
			 * Clear any pending DPM power role swap request so we
			 * don't trigger a power role swap request back to src
			 * power role.
			 */
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_PR_SWAP);
			/*
			 * Power Role Swap OK, transition to
			 * PE_PRS_SRC_SNK_Transition_to_off
			 */
			set_state_pe(port, PE_PRS_SRC_SNK_TRANSITION_TO_OFF);
		} else {
			/* Message sent, return to PE_SRC_Ready */
			set_state_pe(port, PE_SRC_READY);
		}
	}
}

/**
 * PE_PRS_SRC_SNK_Transition_To_Off
 */
static void pe_prs_src_snk_transition_to_off_entry(int port)
{
	print_current_state(port);

	/* Contract is invalid */
	pe_invalidate_explicit_contract(port);

	/* Tell TypeC to power off the source */
	tc_src_power_off(port);
}

static void pe_prs_src_snk_transition_to_off_run(int port)
{
	/*
	 * This is a non-interruptible AMS and power is transitioning - hard
	 * reset on interruption.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		tc_pr_swap_complete(port, 0);
		set_state_pe(port, PE_SRC_HARD_RESET);
	}

	/* Give time for supply to power off */
	if (pd_check_vbus_level(port, VBUS_SAFE0V))
		set_state_pe(port, PE_PRS_SRC_SNK_ASSERT_RD);
}

static void pe_prs_src_snk_transition_to_off_exit(int port)
{
}

/**
 * PE_PRS_SRC_SNK_Assert_Rd
 */
static void pe_prs_src_snk_assert_rd_entry(int port)
{
	print_current_state(port);

	/* Tell TypeC to swap from Attached.SRC to Attached.SNK */
	tc_prs_src_snk_assert_rd(port);
}

static void pe_prs_src_snk_assert_rd_run(int port)
{
	/* Wait until Rd is asserted */
	if (tc_is_attached_snk(port))
		set_state_pe(port, PE_PRS_SRC_SNK_WAIT_SOURCE_ON);
}

/**
 * PE_PRS_SRC_SNK_Wait_Source_On
 */
static void pe_prs_src_snk_wait_source_on_entry(int port)
{
	print_current_state(port);
	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_PS_RDY);
}

static void pe_prs_src_snk_wait_source_on_run(int port)
{
	if (!is_enabled_timer(port, PE_PS_SOURCE_ON) &&
	    PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/* Update pe power role */
		pe[port].power_role = pd_get_power_role(port);
		start_timer(port, PE_PS_SOURCE_ON);
	}

	/*
	 * Transition to PE_SNK_Startup when:
	 *   1) A PS_RDY Message is received.
	 */
	if (is_enabled_timer(port, PE_PS_SOURCE_ON) &&
	    PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		int type = PD_HEADER_TYPE(rx_emsg[port].header);
		int cnt = PD_HEADER_CNT(rx_emsg[port].header);
		int ext = PD_HEADER_EXT(rx_emsg[port].header);

		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		if ((ext == 0) && (cnt == 0) && (type == PD_CTRL_PS_RDY)) {
			PE_SET_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE);
			set_state_pe(port, PE_SNK_STARTUP);
		} else {
			int sop = PD_HEADER_GET_SOP(rx_emsg[port].header);
			/*
			 * USB PD 3.0 6.8.1:
			 * Receiving an unexpected message shall be responded
			 * to with a soft reset message.
			 */
			pe_send_soft_reset(port, sop);
		}
		return;
	}

	/*
	 * Transition to ErrorRecovery state when:
	 *   1) The PSSourceOnTimer times out.
	 *   2) PS_RDY not sent after retries.
	 */
	if (is_expired_timer(port, PE_PS_SOURCE_ON) ||
	    PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);

		set_state_pe(port, PE_WAIT_FOR_ERROR_RECOVERY);
		return;
	}
}

static void pe_prs_src_snk_wait_source_on_exit(int port)
{
	stop_timer(port, PE_PS_SOURCE_ON);
	tc_pr_swap_complete(port,
			    PE_CHK_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE));
}

/**
 * PE_PRS_SRC_SNK_Send_Swap
 */
static void pe_prs_src_snk_send_swap_entry(int port)
{
	print_current_state(port);

	/* Making an attempt to PR_Swap, clear we were possibly waiting */
	stop_timer(port, PE_PR_SWAP_WAIT);

	/* Request the Protocol Layer to send a PR_Swap Message. */
	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_PR_SWAP);
	pe_sender_response_msg_entry(port);
}

static void pe_prs_src_snk_send_swap_run(int port)
{
	int type;
	int cnt;
	int ext;
	enum pe_msg_check msg_check;

	/*
	 * Check the state of the message sent
	 */
	msg_check = pe_sender_response_msg_run(port);

	/*
	 * Transition to PE_PRS_SRC_SNK_Transition_To_Off when:
	 *   1) An Accept Message is received.
	 *
	 * Transition to PE_SRC_Ready state when:
	 *   1) A Reject Message is received.
	 *   2) Or a Wait Message is received.
	 */
	if ((msg_check & PE_MSG_SENT) &&
	    PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0)) {
			if (type == PD_CTRL_ACCEPT) {
				pe[port].src_snk_pr_swap_counter = 0;
				tc_request_power_swap(port);
				set_state_pe(port,
					PE_PRS_SRC_SNK_TRANSITION_TO_OFF);
			} else if (type == PD_CTRL_REJECT) {
				pe[port].src_snk_pr_swap_counter = 0;
				set_state_pe(port, PE_SRC_READY);
			} else if (type == PD_CTRL_WAIT) {
				if (pe[port].src_snk_pr_swap_counter <
				    N_SNK_SRC_PR_SWAP_COUNT) {
					PE_SET_FLAG(port,
						PE_FLAGS_WAITING_PR_SWAP);
					start_timer(port, PE_PR_SWAP_WAIT);
				}
				pe[port].src_snk_pr_swap_counter++;
				set_state_pe(port, PE_SRC_READY);
			}
			return;
		}
	}

	/*
	 * Transition to PE_SRC_Ready state when:
	 *   1) Or the SenderResponseTimer times out.
	 *   2) Message was discarded.
	 */
	if ((msg_check & PE_MSG_DISCARDED) ||
	    is_expired_timer(port, PE_SENDER_RESPONSE))
		set_state_pe(port, PE_SRC_READY);
}

static void pe_prs_src_snk_send_swap_exit(int port)
{
	pe_sender_response_msg_exit(port);
}

/**
 * PE_PRS_SNK_SRC_Evaluate_Swap
 */
static void pe_prs_snk_src_evaluate_swap_entry(int port)
{
	print_current_state(port);

	/*
	 * Cancel any pending PR swap request due to a received Wait since the
	 * partner just sent us a PR swap message.
	 */
	PE_CLR_FLAG(port, PE_FLAGS_WAITING_PR_SWAP);
	pe[port].src_snk_pr_swap_counter = 0;

	if (!pd_check_power_swap(port)) {
		/* PE_PRS_SNK_SRC_Reject_Swap state embedded here */
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_REJECT);
	} else {
		tc_request_power_swap(port);
		/* PE_PRS_SNK_SRC_Accept_Swap state embedded here */
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_ACCEPT);
	}
}

static void pe_prs_snk_src_evaluate_swap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		if (PE_CHK_FLAG(port, PE_FLAGS_ACCEPT)) {
			PE_CLR_FLAG(port, PE_FLAGS_ACCEPT);

			/*
			 * Clear any pending DPM power role swap request so we
			 * don't trigger a power role swap request back to sink
			 * power role.
			 */
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_PR_SWAP);
			/*
			 * Accept message sent, transition to
			 * PE_PRS_SNK_SRC_Transition_to_off
			 */
			set_state_pe(port, PE_PRS_SNK_SRC_TRANSITION_TO_OFF);
		} else {
			/* Message sent, return to PE_SNK_Ready */
			set_state_pe(port, PE_SNK_READY);
		}
	}

	if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
		/*
		 * Protocol Error occurs while PR swap, this may
		 * brown out if the port-parnter can't hold VBUS
		 * for tSrcTransition. Notify TC that we end the PR
		 * swap and start to watch VBUS.
		 *
		 * TODO(b:155181980): issue soft reset on protocol error.
		 */
		tc_pr_swap_complete(port, 0);
	}
}

/**
 * PE_PRS_SNK_SRC_Transition_To_Off
 * PE_FRS_SNK_SRC_Transition_To_Off
 *
 * NOTE: Shared action code used for Power Role Swap and Fast Role Swap
 */
static void pe_prs_snk_src_transition_to_off_entry(int port)
{
	print_current_state(port);

	tc_snk_power_off(port);
	start_timer(port, PE_PS_SOURCE_OFF);
}

static void pe_prs_snk_src_transition_to_off_run(int port)
{
	int type;
	int cnt;
	int ext;

	/*
	 * Transition to ErrorRecovery state when:
	 *   1) The PSSourceOffTimer times out.
	 */
	if (is_expired_timer(port, PE_PS_SOURCE_ON))
		set_state_pe(port, PE_WAIT_FOR_ERROR_RECOVERY);

	/*
	 * Transition to PE_PRS_SNK_SRC_Assert_Rp when:
	 *   1) An PS_RDY Message is received.
	 */
	else if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0) && (type == PD_CTRL_PS_RDY)) {
			/*
			 * FRS: We are always ready to drive vSafe5v, so just
			 * skip PE_FRS_SNK_SRC_Vbus_Applied and go direct to
			 * PE_FRS_SNK_SRC_Assert_Rp
			 */
			set_state_pe(port, PE_PRS_SNK_SRC_ASSERT_RP);
		}
	}
}

static void pe_prs_snk_src_transition_to_off_exit(int port)
{
	stop_timer(port, PE_PS_SOURCE_ON);
}

/**
 * PE_PRS_SNK_SRC_Assert_Rp
 * PE_FRS_SNK_SRC_Assert_Rp
 *
 * NOTE: Shared action code used for Power Role Swap and Fast Role Swap
 */
static void pe_prs_snk_src_assert_rp_entry(int port)
{
	print_current_state(port);

	/*
	 * Tell TypeC to Power/Fast Role Swap (PRS/FRS) from
	 * Attached.SNK to Attached.SRC
	 */
	tc_prs_snk_src_assert_rp(port);
}

static void pe_prs_snk_src_assert_rp_run(int port)
{
	/* Wait until TypeC is in the Attached.SRC state */
	if (tc_is_attached_src(port)) {
		/* Contract is invalid now */
		pe_invalidate_explicit_contract(port);
		set_state_pe(port, PE_PRS_SNK_SRC_SOURCE_ON);
	}
}

/**
 * PE_PRS_SNK_SRC_Source_On
 * PE_FRS_SNK_SRC_Source_On
 *
 * NOTE: Shared action code used for Power Role Swap and Fast Role Swap
 */
static void pe_prs_snk_src_source_on_entry(int port)
{
	print_current_state(port);
}

static void pe_prs_snk_src_source_on_run(int port)
{
	/* update pe power role */
	pe[port].power_role = pd_get_power_role(port);
	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_PS_RDY);

	/*
	 * Transition to ErrorRecovery state when:
	 *   1) On protocol error
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
		set_state_pe(port, PE_WAIT_FOR_ERROR_RECOVERY);
	} else if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/* Run swap source timer on entry to pe_src_startup */
		PE_SET_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE);
		set_state_pe(port, PE_SRC_STARTUP);
	}
}

static void pe_prs_snk_src_source_on_exit(int port)
{
	tc_pr_swap_complete(port,
			    PE_CHK_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE));
}

/**
 * PE_PRS_SNK_SRC_Send_Swap
 * PE_FRS_SNK_SRC_Send_Swap
 *
 * NOTE: Shared action code used for Power Role Swap and Fast Role Swap
 */
static void pe_prs_snk_src_send_swap_entry(int port)
{
	print_current_state(port);

	/*
	 * PRS_SNK_SRC_SEND_SWAP
	 *     Request the Protocol Layer to send a PR_Swap Message.
	 *
	 * FRS_SNK_SRC_SEND_SWAP
	 *     Hardware should have turned off sink power and started
	 *     bringing Vbus to vSafe5.
	 *     Request the Protocol Layer to send a FR_Swap Message.
	 */
	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_PR_SWAP);
	pe_sender_response_msg_entry(port);
}

static void pe_prs_snk_src_send_swap_run(int port)
{
	int type;
	int cnt;
	int ext;
	enum pe_msg_check msg_check;

	/*
	 * Check the state of the message sent
	 */
	msg_check = pe_sender_response_msg_run(port);

	/*
	 * Handle discarded message
	 */
	if (msg_check & PE_MSG_DISCARDED) {
		set_state_pe(port, PE_SNK_READY);
		return;
	}

	/*
	 * Transition to PE_PRS_SNK_SRC_Transition_to_off when:
	 *   1) An Accept Message is received.
	 *
	 * PRS: Transition to PE_SNK_Ready state when:
	 * FRS: Transition to ErrorRecovery state when:
	 *   1) A Reject Message is received.
	 *   2) Or a Wait Message is received.
	 */
	if ((msg_check & PE_MSG_SENT) &&
	    PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0)) {
			if (type == PD_CTRL_ACCEPT) {
				tc_request_power_swap(port);
				set_state_pe(port,
					     PE_PRS_SNK_SRC_TRANSITION_TO_OFF);
			} else if ((type == PD_CTRL_REJECT) ||
						(type == PD_CTRL_WAIT)) {
				set_state_pe(port, PE_SNK_READY);
			}
			return;
		}
	}

	/*
	 * PRS: Transition to PE_SNK_Ready state when:
	 *   1) The SenderResponseTimer times out.
	 */
	if (is_expired_timer(port, PE_SENDER_RESPONSE))
		set_state_pe(port, PE_SNK_READY);
}

static void pe_prs_snk_src_send_swap_exit(int port)
{
	pe_sender_response_msg_exit(port);
}

/**
 * Give_Sink_Cap Message
 */
static void pe_snk_give_sink_cap_entry(int port)
{
	print_current_state(port);

	/* Send a Sink_Capabilities Message */
	tx_emsg[port].len = pd_snk_pdo_cnt * 4;
	memcpy(tx_emsg[port].buf, (uint8_t *)pd_snk_pdo, tx_emsg[port].len);
	send_data_msg(port, TCPCI_MSG_SOP, PD_DATA_SINK_CAP);
}

static void pe_snk_give_sink_cap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		pe_set_ready_state(port);
		return;
	}

	if (pe_check_outgoing_discard(port))
		return;
}

/**
 * Wait For Error Recovery
 */
static void pe_wait_for_error_recovery_entry(int port)
{
	print_current_state(port);
	tc_start_error_recovery(port);
}

static void pe_wait_for_error_recovery_run(int port)
{
	/* Stay here until error recovery is complete */
}

/**
 * PE_VDM_Response
 */
static void pe_vdm_response_entry(int port)
{
	int vdo_len = 0;
	uint32_t *rx_payload;
	uint32_t *tx_payload;
	uint8_t vdo_cmd;
	svdm_rsp_func func = NULL;

	print_current_state(port);

	/* This is an Interruptible AMS */
	PE_SET_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);

	/* Get the message */
	rx_payload = (uint32_t *)rx_emsg[port].buf;

	/* Extract VDM command from the VDM header */
	vdo_cmd = PD_VDO_CMD(rx_payload[0]);
	/* This must be a command request to proceed further */
	if (PD_VDO_CMDT(rx_payload[0]) != CMDT_INIT) {
		CPRINTF("ERR:CMDT:%d:%d\n", PD_VDO_CMDT(rx_payload[0]),
			vdo_cmd);

		pe_set_ready_state(port);
		return;
	}

	tx_payload = (uint32_t *)tx_emsg[port].buf;
	/*
	 * Designed in TCPMv1, svdm_response functions use same
	 * buffer to take received data and overwrite with response
	 * data. To work with this interface, here copy rx data to
	 * tx buffer and pass tx_payload to func.
	 * TODO(b/166455363): change the interface to pass both rx
	 * and tx buffer.
	 *
	 * The SVDM header is dependent on both VDM command request being
	 * replied to and the result of response function. The SVDM command
	 * message is copied into tx_payload. tx_payload[0] is the VDM header
	 * for the response message. The SVDM response function takes the role
	 * of the DPM layer and will indicate the response type (ACK/NAK/BUSY)
	 * by its return value (vdo_len)
	 *    vdo_len > 0  --> ACK
	 *    vdo_len == 0 --> NAK
	 *    vdo_len < 0  --> BUSY
	 */
	memcpy(tx_payload, rx_payload, PD_HEADER_CNT(rx_emsg[port].header) * 4);
	/*
	 * Clear fields in SVDM response message that will be set based on the
	 * result of the svdm response function.
	 */
	tx_payload[0] &= ~VDO_CMDT_MASK;
	tx_payload[0] &= ~VDO_SVDM_VERS(0x3);

	/* Add SVDM structured version being used */
	tx_payload[0] |= VDO_SVDM_VERS(0);

	/* Use VDM command to select the response handler function */
	switch (vdo_cmd) {
	case CMD_DISCOVER_IDENT:
		func = svdm_rsp.identity;
		break;
	case CMD_DISCOVER_SVID:
		func = svdm_rsp.svids;
		break;
	case CMD_DISCOVER_MODES:
		func = svdm_rsp.modes;
		break;
	case CMD_ENTER_MODE:
		func = svdm_rsp.enter_mode;
		break;
	case CMD_DP_STATUS:
		if (svdm_rsp.amode)
			func = svdm_rsp.amode->status;
		break;
	case CMD_DP_CONFIG:
		if (svdm_rsp.amode)
			func = svdm_rsp.amode->config;
		break;
	case CMD_EXIT_MODE:
		func = svdm_rsp.exit_mode;
		break;
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	case CMD_ATTENTION:
		/*
		 * attention is only SVDM with no response
		 * (just goodCRC) return zero here.
		 */
		dfp_consume_attention(port, rx_payload);
		pe_set_ready_state(port);
		return;
#endif
	default:
		CPRINTF("VDO ERR:CMD:%d\n", vdo_cmd);
	}

	/*
	 * If the port partner is PD_REV20 and our data role is DFP, we must
	 * reply to any SVDM command with a NAK. If the SVDM was an Attention
	 * command, it does not have a response, and exits the function above.
	 */
	if (func && (prl_get_rev(port, TCPCI_MSG_SOP) != PD_REV20 ||
		     pe[port].data_role == PD_ROLE_UFP)) {
		/*
		 * Execute SVDM response function selected above and set the
		 * correct response type in the VDM header.
		 */
		vdo_len = func(port, tx_payload);
		if (vdo_len > 0) {
			tx_payload[0] |= VDO_CMDT(CMDT_RSP_ACK);
			/*
			 * If command response is an ACK and if the command was
			 * either enter/exit mode, then update the PE modal flag
			 * accordingly.
			 */
			if (vdo_cmd == CMD_ENTER_MODE)
				PE_SET_FLAG(port, PE_FLAGS_MODAL_OPERATION);
			if (vdo_cmd == CMD_EXIT_MODE)
				PE_CLR_FLAG(port, PE_FLAGS_MODAL_OPERATION);
		} else if (!vdo_len) {
			tx_payload[0] |= VDO_CMDT(CMDT_RSP_NAK);
			vdo_len = 1;
		} else {
			tx_payload[0] |= VDO_CMDT(CMDT_RSP_BUSY);
			vdo_len = 1;
		}
	} else {
		/*
		 * Received at VDM command which is not supported.  PD 2.0 may
		 * NAK or ignore the message (see TD.PD.VNDI.E1. VDM Identity
		 * steps), but PD 3.0 must send Not_Supported (PD 3.0 Ver 2.0 +
		 * ECNs 2020-12-10 Table 6-64 Response to an incoming
		 * VDM or TD.PD.VNDI3.E3 VDM Identity steps)
		 */
		if (prl_get_rev(port, TCPCI_MSG_SOP) == PD_REV30) {
			set_state_pe(port, PE_SEND_NOT_SUPPORTED);
			return;
		}
		tx_payload[0] |= VDO_CMDT(CMDT_RSP_NAK);
		vdo_len = 1;
	}

	/* Send response message. Note len is in bytes, not VDO objects */
	tx_emsg[port].len = (vdo_len * sizeof(uint32_t));
	send_data_msg(port, TCPCI_MSG_SOP, PD_DATA_VENDOR_DEF);
}

static void pe_vdm_response_run(int port)
{
	/*
	 * This state waits for a VDM response message to be sent. Return to the
	 * ready state once the message has been sent, a protocol error was
	 * detected, or if the VDM response msg was discarded based on being
	 * interrupted by another rx message. Since VDM sequences are AMS
	 * interruptible, there is no need to soft reset regardless of exit
	 * reason.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE) ||
	    PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR) ||
	    PE_CHK_FLAG(port, PE_FLAGS_MSG_DISCARDED)) {

		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE |
			    PE_FLAGS_PROTOCOL_ERROR |
			    PE_FLAGS_MSG_DISCARDED);

		pe_set_ready_state(port);
	}
}

static void pe_vdm_response_exit(int port)
{
	PE_CLR_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);
}

/*
 * PE_DR_SNK_Get_Sink_Cap and PE_SRC_Get_Sink_Cap State (shared)
 */
static void pe_dr_get_sink_cap_entry(int port)
{
	print_current_state(port);

	/* Send a Get Sink Cap Message */
	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_GET_SINK_CAP);
	pe_sender_response_msg_entry(port);
}

static void pe_dr_get_sink_cap_run(int port)
{
	int type;
	int cnt;
	int ext;
	enum pe_msg_check msg_check;
	enum tcpci_msg_type sop;

	/*
	 * Check the state of the message sent
	 */
	msg_check = pe_sender_response_msg_run(port);

	/*
	 * Transition to PE_[SRC,SNK]_Ready when:
	 *   1) A Sink_Capabilities Message is received
	 *   2) Or SenderResponseTimer times out
	 *   3) Or a Reject Message is received.
	 *
	 * Transition to PE_SEND_SOFT_RESET state when:
	 *   1) An unexpected message is received
	 */
	if ((msg_check & PE_MSG_SENT) &&
	    PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);
		sop = PD_HEADER_GET_SOP(rx_emsg[port].header);

		if (ext == 0 && sop == TCPCI_MSG_SOP) {
			if ((cnt > 0) && (type == PD_DATA_SINK_CAP)) {
				uint32_t *payload =
					(uint32_t *)rx_emsg[port].buf;
				uint8_t cap_cnt = rx_emsg[port].len /
							sizeof(uint32_t);

				pe_set_snk_caps(port, cap_cnt, payload);

				pe_set_ready_state(port);
				return;
			} else if (cnt == 0 && (type == PD_CTRL_REJECT ||
				   type == PD_CTRL_NOT_SUPPORTED)) {
				pe_set_ready_state(port);
				return;
			}
			/* Unexpected messages fall through to soft reset */
		}

		pe_send_soft_reset(port, sop);
		return;
	}

	/*
	 * Transition to PE_[SRC,SNK]_Ready state when:
	 *   1) SenderResponseTimer times out.
	 *   2) Message was discarded.
	 */
	if ((msg_check & PE_MSG_DISCARDED) ||
	    is_expired_timer(port, PE_SENDER_RESPONSE)) {
		pe_set_ready_state(port);
	}
}

static void pe_dr_get_sink_cap_exit(int port)
{
	pe_sender_response_msg_exit(port);
}

/*
 * PE_DR_SNK_Give_Source_Cap
 */
static void pe_dr_snk_give_source_cap_entry(int port)
{
	print_current_state(port);

	/* Send source capabilities. */
	send_source_cap(port);
}

static void pe_dr_snk_give_source_cap_run(int port)
{
	/*
	 * Transition back to PE_SNK_Ready when the Source_Capabilities message
	 * has been successfully sent.
	 *
	 * Get Source Capabilities AMS is uninterruptible, but in case the
	 * partner violates the spec then send a soft reset rather than get
	 * stuck here.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		set_state_pe(port, PE_SNK_READY);
	} else if (PE_CHK_FLAG(port, PE_FLAGS_MSG_DISCARDED)) {
		pe_send_soft_reset(port, TCPCI_MSG_SOP);
	}
}

/*
 * PE_DR_SRC_Get_Source_Cap
 */
static void pe_dr_src_get_source_cap_entry(int port)
{
	print_current_state(port);

	/* Send a Get_Source_Cap Message */
	tx_emsg[port].len = 0;
	send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_GET_SOURCE_CAP);
	pe_sender_response_msg_entry(port);
}

static void pe_dr_src_get_source_cap_run(int port)
{
	int type;
	int cnt;
	int ext;
	enum pe_msg_check msg_check;

	/*
	 * Check the state of the message sent
	 */
	msg_check = pe_sender_response_msg_run(port);

	/*
	 * Transition to PE_SRC_Ready when:
	 *   1) A Source Capabilities Message is received.
	 *   2) A Reject Message is received.
	 */
	if ((msg_check & PE_MSG_SENT) &&
	    PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if (ext == 0) {
			if ((cnt > 0) && (type == PD_DATA_SOURCE_CAP)) {
				uint32_t *payload =
					(uint32_t *)rx_emsg[port].buf;

				pd_set_src_caps(port, cnt, payload);

				/*
				 * If we'd prefer to charge from this partner,
				 * then propose a PR swap.
				 */
				if (pd_can_charge_from_device(port, cnt,
							      payload))
					pd_request_power_swap(port);

				/*
				 * Report dual role power capability to the
				 * charge manager if present
				 */
				if (IS_ENABLED(CONFIG_CHARGE_MANAGER) &&
				    pd_get_partner_dual_role_power(port))
					charge_manager_update_dualrole(port,
								CAP_DUALROLE);

				set_state_pe(port, PE_SRC_READY);
			} else if ((cnt == 0) && (type == PD_CTRL_REJECT ||
					type == PD_CTRL_NOT_SUPPORTED)) {
				pd_set_src_caps(port, -1, NULL);
				set_state_pe(port, PE_SRC_READY);
			} else {
				/*
				 * On protocol error, consider source cap
				 * retrieval a failure
				 */
				pd_set_src_caps(port, -1, NULL);
				set_state_pe(port, PE_SEND_SOFT_RESET);
			}
			return;
		} else {
			pd_set_src_caps(port, -1, NULL);
			set_state_pe(port, PE_SEND_SOFT_RESET);
			return;
		}
	}

	/*
	 * Transition to PE_SRC_Ready state when:
	 *   1) the SenderResponseTimer times out.
	 *   2) Message was discarded.
	 */
	if ((msg_check & PE_MSG_DISCARDED) ||
	    is_expired_timer(port, PE_SENDER_RESPONSE))
		set_state_pe(port, PE_SRC_READY);
}

static void pe_dr_src_get_source_cap_exit(int port)
{
	pe_sender_response_msg_exit(port);
}

const uint32_t * const pd_get_src_caps(int port)
{
	return pe[port].src_caps;
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
	int i;

	pe[port].src_cap_cnt = cnt;

	for (i = 0; i < cnt; i++)
		pe[port].src_caps[i] = *src_caps++;
}

uint8_t pd_get_src_cap_cnt(int port)
{
	if (pe[port].src_cap_cnt > 0)
		return pe[port].src_cap_cnt;

	return 0;
}

const char *pe_get_current_state(int port)
{
	if (pe_is_running(port) && IS_ENABLED(USB_PD_DEBUG_LABELS))
		return pe_state_names[get_state_pe(port)];
	else
		return "";
}

uint32_t pe_get_flags(int port)
{
	return pe[port].flags;
}

static __const_data const struct usb_state pe_states[] = {
	/* Normal States */
	[PE_SRC_STARTUP] = {
		.entry = pe_src_startup_entry,
		.run   = pe_src_startup_run,
		.exit  = pe_src_startup_exit,
	},
	[PE_SRC_DISCOVERY] = {
		.entry = pe_src_discovery_entry,
		.run   = pe_src_discovery_run,
	},
	[PE_SRC_SEND_CAPABILITIES] = {
		.entry = pe_src_send_capabilities_entry,
		.run   = pe_src_send_capabilities_run,
		.exit  = pe_src_send_capabilities_exit,
	},
	[PE_SRC_NEGOTIATE_CAPABILITY] = {
		.entry = pe_src_negotiate_capability_entry,
	},
	[PE_SRC_TRANSITION_SUPPLY] = {
		.entry = pe_src_transition_supply_entry,
		.run   = pe_src_transition_supply_run,
		.exit  = pe_src_transition_supply_exit,
	},
	[PE_SRC_READY] = {
		.entry = pe_src_ready_entry,
		.run   = pe_src_ready_run,
	},
	[PE_SRC_DISABLED] = {
		.entry = pe_src_disabled_entry,
	},
	[PE_SRC_CAPABILITY_RESPONSE] = {
		.entry = pe_src_capability_response_entry,
		.run   = pe_src_capability_response_run,
	},
	[PE_SRC_HARD_RESET] = {
		.entry = pe_src_hard_reset_entry,
		.run   = pe_src_hard_reset_run,
		.exit  = pe_src_hard_reset_exit,
	},
	[PE_SRC_HARD_RESET_RECEIVED] = {
		.entry = pe_src_hard_reset_received_entry,
		.run = pe_src_hard_reset_received_run,
		.exit = pe_src_hard_reset_received_exit,
	},
	[PE_SRC_TRANSITION_TO_DEFAULT] = {
		.entry = pe_src_transition_to_default_entry,
		.run = pe_src_transition_to_default_run,
	},
	[PE_SNK_STARTUP] = {
		.entry = pe_snk_startup_entry,
		.run = pe_snk_startup_run,
	},
	[PE_SNK_DISCOVERY] = {
		.entry = pe_snk_discovery_entry,
		.run = pe_snk_discovery_run,
	},
	[PE_SNK_WAIT_FOR_CAPABILITIES] = {
		.entry = pe_snk_wait_for_capabilities_entry,
		.run = pe_snk_wait_for_capabilities_run,
		.exit = pe_snk_wait_for_capabilities_exit,
	},
	[PE_SNK_EVALUATE_CAPABILITY] = {
		.entry = pe_snk_evaluate_capability_entry,
	},
	[PE_SNK_SELECT_CAPABILITY] = {
		.entry = pe_snk_select_capability_entry,
		.run = pe_snk_select_capability_run,
		.exit = pe_snk_select_capability_exit,
	},
	[PE_SNK_READY] = {
		.entry = pe_snk_ready_entry,
		.run   = pe_snk_ready_run,
	},
	[PE_SNK_HARD_RESET] = {
		.entry = pe_snk_hard_reset_entry,
		.run   = pe_snk_hard_reset_run,
	},
	[PE_SNK_TRANSITION_TO_DEFAULT] = {
		.entry = pe_snk_transition_to_default_entry,
		.run   = pe_snk_transition_to_default_run,
	},
	[PE_SNK_GIVE_SINK_CAP] = {
		.entry = pe_snk_give_sink_cap_entry,
		.run = pe_snk_give_sink_cap_run,
	},
	[PE_SNK_GET_SOURCE_CAP] = {
		.entry = pe_snk_get_source_cap_entry,
		.run   = pe_snk_get_source_cap_run,
	},
	[PE_SNK_TRANSITION_SINK] = {
		.entry = pe_snk_transition_sink_entry,
		.run   = pe_snk_transition_sink_run,
		.exit   = pe_snk_transition_sink_exit,
	},
	[PE_SEND_SOFT_RESET] = {
		.entry = pe_send_soft_reset_entry,
		.run = pe_send_soft_reset_run,
		.exit = pe_send_soft_reset_exit,
	},
	[PE_SOFT_RESET] = {
		.entry = pe_soft_reset_entry,
		.run = pe_soft_reset_run,
	},
	[PE_SEND_NOT_SUPPORTED] = {
		.entry = pe_send_not_supported_entry,
		.run = pe_send_not_supported_run,
	},
	[PE_SRC_PING] = {
		.entry = pe_src_ping_entry,
		.run   = pe_src_ping_run,
	},
	[PE_DRS_EVALUATE_SWAP] = {
		.entry = pe_drs_evaluate_swap_entry,
		.run   = pe_drs_evaluate_swap_run,
	},
	[PE_DRS_CHANGE] = {
		.entry = pe_drs_change_entry,
		.run   = pe_drs_change_run,
	},
	[PE_DRS_SEND_SWAP] = {
		.entry = pe_drs_send_swap_entry,
		.run   = pe_drs_send_swap_run,
		.exit  = pe_drs_send_swap_exit,
	},
	[PE_PRS_SRC_SNK_EVALUATE_SWAP] = {
		.entry = pe_prs_src_snk_evaluate_swap_entry,
		.run   = pe_prs_src_snk_evaluate_swap_run,
	},
	[PE_PRS_SRC_SNK_TRANSITION_TO_OFF] = {
		.entry = pe_prs_src_snk_transition_to_off_entry,
		.run   = pe_prs_src_snk_transition_to_off_run,
		.exit  = pe_prs_src_snk_transition_to_off_exit,
	},
	[PE_PRS_SRC_SNK_ASSERT_RD] = {
		.entry = pe_prs_src_snk_assert_rd_entry,
		.run   = pe_prs_src_snk_assert_rd_run,
	},
	[PE_PRS_SRC_SNK_WAIT_SOURCE_ON] = {
		.entry = pe_prs_src_snk_wait_source_on_entry,
		.run   = pe_prs_src_snk_wait_source_on_run,
		.exit  = pe_prs_src_snk_wait_source_on_exit,
	},
	[PE_PRS_SRC_SNK_SEND_SWAP] = {
		.entry = pe_prs_src_snk_send_swap_entry,
		.run   = pe_prs_src_snk_send_swap_run,
		.exit  = pe_prs_src_snk_send_swap_exit,
	},
	[PE_PRS_SNK_SRC_EVALUATE_SWAP] = {
		.entry = pe_prs_snk_src_evaluate_swap_entry,
		.run   = pe_prs_snk_src_evaluate_swap_run,
	},
	/*
	 * Some of the Power Role Swap actions are shared with the very
	 * similar actions of Fast Role Swap.
	 */
	/* State actions are shared with PE_FRS_SNK_SRC_TRANSITION_TO_OFF */
	[PE_PRS_SNK_SRC_TRANSITION_TO_OFF] = {
		.entry = pe_prs_snk_src_transition_to_off_entry,
		.run   = pe_prs_snk_src_transition_to_off_run,
		.exit  = pe_prs_snk_src_transition_to_off_exit,
	},
	/* State actions are shared with PE_FRS_SNK_SRC_ASSERT_RP */
	[PE_PRS_SNK_SRC_ASSERT_RP] = {
		.entry = pe_prs_snk_src_assert_rp_entry,
		.run   = pe_prs_snk_src_assert_rp_run,
	},
	/* State actions are shared with PE_FRS_SNK_SRC_SOURCE_ON */
	[PE_PRS_SNK_SRC_SOURCE_ON] = {
		.entry = pe_prs_snk_src_source_on_entry,
		.run   = pe_prs_snk_src_source_on_run,
		.exit  = pe_prs_snk_src_source_on_exit,
	},
	/* State actions are shared with PE_FRS_SNK_SRC_SEND_SWAP */
	[PE_PRS_SNK_SRC_SEND_SWAP] = {
		.entry = pe_prs_snk_src_send_swap_entry,
		.run   = pe_prs_snk_src_send_swap_run,
		.exit  = pe_prs_snk_src_send_swap_exit,
	},
	[PE_VDM_RESPONSE] = {
		.entry = pe_vdm_response_entry,
		.run   = pe_vdm_response_run,
		.exit  = pe_vdm_response_exit,
	},
	[PE_WAIT_FOR_ERROR_RECOVERY] = {
		.entry = pe_wait_for_error_recovery_entry,
		.run   = pe_wait_for_error_recovery_run,
	},
	[PE_DR_GET_SINK_CAP] = {
		.entry = pe_dr_get_sink_cap_entry,
		.run   = pe_dr_get_sink_cap_run,
		.exit  = pe_dr_get_sink_cap_exit,
	},
	[PE_DR_SNK_GIVE_SOURCE_CAP] = {
		.entry = pe_dr_snk_give_source_cap_entry,
		.run = pe_dr_snk_give_source_cap_run,
	},
	[PE_DR_SRC_GET_SOURCE_CAP] = {
		.entry = pe_dr_src_get_source_cap_entry,
		.run   = pe_dr_src_get_source_cap_run,
		.exit  = pe_dr_src_get_source_cap_exit,
	},
};
