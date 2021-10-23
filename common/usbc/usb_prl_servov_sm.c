/* Copyright 2022 The Chromium OS Authors. All rights reserved.
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
#include "cros_version.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "util.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"
#include "usb_emsg.h"
#include "usb_sm.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

#define SET_FLAG(port, flag) atomic_or(&prl_flags[port], (flag))
#define CLR_FLAG(port, flag) atomic_clear_bits(&prl_flags[port], (flag))
#define CHK_FLAG(port, flag) (prl_flags[port] & (flag))

/* Protocol Layer Flags */
/*
 * NOTE:
 *	These flags are used in multiple state machines and could have
 *	different meanings in each state machine.
 */
/* Flag to note message transmission completed */
#define PRL_FLAGS_TX_COMPLETE             BIT(0)
/* Flag to note transmission error occurred */
#define PRL_FLAGS_TX_ERROR                BIT(1)
/* Flag to note PE triggered a hard reset */
#define PRL_FLAGS_PE_HARD_RESET           BIT(2)
/* Flag to note hard reset has completed */
#define PRL_FLAGS_HARD_RESET_COMPLETE     BIT(3)
/* Flag to note port partner sent a hard reset */
#define PRL_FLAGS_PORT_PARTNER_HARD_RESET BIT(4)
/*
 * Flag to note a message transmission has been requested. It is only cleared
 * when we send the message to the TCPC layer.
 */
#define PRL_FLAGS_MSG_XMIT                BIT(5)
/* Flag to note a message was received */
#define PRL_FLAGS_MSG_RECEIVED            BIT(6)
/* Flag to note aborting current TX message, not currently set */
#define PRL_FLAGS_ABORT                   BIT(7)

/* PD counter definitions */
#define PD_MESSAGE_ID_COUNT 7

#define HARD_RESET_FLAGS	(PRL_FLAGS_PE_HARD_RESET | \
				PRL_FLAGS_HARD_RESET_COMPLETE | \
				PRL_FLAGS_PORT_PARTNER_HARD_RESET)
#define PDMSG_FLAGS		(PRL_FLAGS_TX_COMPLETE | PRL_FLAGS_TX_ERROR)


/* Size of PDMSG Chunk Buffer */
#define CHK_BUF_SIZE 7
#define CHK_BUF_SIZE_BYTES 28

/*
 * Debug log level - higher number == more log
 *   Level 0: disabled
 *   Level 1: not currently used
 *   Level 2: plus non-ping messages
 *   Level 3: plus ping packet and PRL states
 *
 * Note that higher log level causes timing changes and thus may affect
 * performance.
 */
#ifdef CONFIG_USB_PD_DEBUG_LEVEL
static const enum debug_level prl_debug_level = CONFIG_USB_PD_DEBUG_LEVEL;
#else
static enum debug_level prl_debug_level = DEBUG_LEVEL_1;
#endif

static enum sm_local_state local_state[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Protocol Transmit States (Section 6.11.2.2) */
enum usb_prl_tx_state {
	PRL_TX_PHY_LAYER_RESET,
	PRL_TX_WAIT_FOR_MESSAGE_REQUEST,
	PRL_TX_LAYER_RESET_FOR_TRANSMIT,
	PRL_TX_WAIT_FOR_PHY_RESPONSE,
	PRL_TX_SRC_SOURCE_TX,
	PRL_TX_SNK_START_AMS,
	PRL_TX_SRC_PENDING,
	PRL_TX_SNK_PENDING,
	PRL_TX_DISCARD_MESSAGE,
};

/* Protocol Hard Reset States (Section 6.11.2.4) */
enum usb_prl_hr_state {
	PRL_HR_WAIT_FOR_REQUEST,
	PRL_HR_RESET_LAYER,
	PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE,
	PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE,
};

static const char * const prl_tx_state_names[] = {
	[PRL_TX_PHY_LAYER_RESET] = "PRL_TX_PHY_LAYER_RESET",
	[PRL_TX_WAIT_FOR_MESSAGE_REQUEST] = "PRL_TX_WAIT_FOR_MESSAGE_REQUEST",
	[PRL_TX_LAYER_RESET_FOR_TRANSMIT] = "PRL_TX_LAYER_RESET_FOR_TRANSMIT",
	[PRL_TX_WAIT_FOR_PHY_RESPONSE] = "PRL_TX_WAIT_FOR_PHY_RESPONSE",
	[PRL_TX_SRC_SOURCE_TX] = "PRL_TX_SRC_SOURCE_TX",
	[PRL_TX_SNK_START_AMS] = "PRL_TX_SNK_START_AMS",
	[PRL_TX_SRC_PENDING] = "PRL_TX_SRC_PENDING",
	[PRL_TX_SNK_PENDING] = "PRL_TX_SNK_PENDING",
	[PRL_TX_DISCARD_MESSAGE] = "PRL_TX_DISCARD_MESSAGE",
};

static const char * const prl_hr_state_names[] = {
	[PRL_HR_WAIT_FOR_REQUEST] = "PRL_HR_WAIT_FOR_REQUEST",
	[PRL_HR_RESET_LAYER] = "PRL_HR_RESET_LAYER",
	[PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE]
		= "PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE",
	[PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE]
		= "PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE",
};

/* Forward declare full list of states. Index by above enums. */
static const struct usb_state prl_tx_states[];
static const struct usb_state prl_hr_states[];

/* Message Reception State Machine Object */
static struct protocol_layer_rx {
	/* received message type */
	enum tcpci_msg_type sop;
	/* message ids for all valid port partners */
	int8_t msg_id[NUM_SOP_STAR_TYPES];
} prl_rx[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Message Transmission State Machine Object */
static struct protocol_layer_tx {
	/* state machine context */
	struct sm_ctx ctx;
	/* last message type we transmitted */
	enum tcpci_msg_type last_xmit_type;
	/* message id counters for all 6 port partners */
	uint8_t msg_id_counter[NUM_SOP_STAR_TYPES];
	/* transmit status */
	int xmit_status;
} prl_tx[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Hard Reset State Machine Object */
static struct protocol_hard_reset {
	/* state machine context */
	struct sm_ctx ctx;
} prl_hr[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Chunking Message Object */
static struct pd_message {
	/* SOP* */
	enum tcpci_msg_type xmit_type;
	/* type of message */
	uint8_t msg_type;
	/* PD revision */
	enum pd_rev_type rev[NUM_SOP_STAR_TYPES];
	/* Number of 32-bit objects in chk_buf */
	uint16_t data_objs;
	/* temp chunk buffer */
	uint32_t tx_chk_buf[CHK_BUF_SIZE];
	uint32_t rx_chk_buf[CHK_BUF_SIZE];
	uint32_t num_bytes_received;
} pdmsg[CONFIG_USB_PD_PORT_MAX_COUNT];

static atomic_t prl_flags[CONFIG_USB_PD_PORT_MAX_COUNT];

struct extended_msg rx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];
struct extended_msg tx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Common Protocol Layer Message Transmission */
static void prl_tx_construct_message(int port);
static void prl_rx_wait_for_phy_message(const int port, int evt);
static void prl_copy_msg_to_buffer(int port);

/*
 * 7-bit timers running at 5ms
 *
 * Bit 7 is set if the timer is enabled, else its disabled
 * Bits 6:0 are incremented every 5ms for a max time of 635ms
 */
#define TIMER_ENABLE 0x80
#define T_LOOP_5MS              (5 * MSEC)

#define PRL_T_TCPC_TX_TIMEOUT  ((100*MSEC) / T_LOOP_5MS)
#define PRL_T_SINK_TX          (PD_T_SINK_TX / T_LOOP_5MS)
#define PRL_T_PS_HARD_RESET    (PD_T_PS_HARD_RESET / T_LOOP_5MS)

enum timer_t {
	PRL_TCPC_TX_TIMEOUT = 0,
	PRL_SINK_TX,
	PRL_PS_HARD_RESET,
};

static uint8_t prl_tcpc_tx_timeout_timer[CONFIG_USB_PD_PORT_MAX_COUNT];
static uint8_t prl_sink_tx_timer[CONFIG_USB_PD_PORT_MAX_COUNT];
static uint8_t prl_ps_hard_reset_timer[CONFIG_USB_PD_PORT_MAX_COUNT];

static void init_timers(int port)
{
	prl_tcpc_tx_timeout_timer[port] = 0;
	prl_sink_tx_timer[port] = 0;
	prl_ps_hard_reset_timer[port] = 0;
}

static void stop_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case PRL_TCPC_TX_TIMEOUT:
		prl_tcpc_tx_timeout_timer[port] = 0;
		break;
	case PRL_SINK_TX:
		prl_sink_tx_timer[port] = 0;
		break;
	case PRL_PS_HARD_RESET:
		prl_ps_hard_reset_timer[port] = 0;
		break;
	}
}

static void start_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case PRL_TCPC_TX_TIMEOUT:
		prl_tcpc_tx_timeout_timer[port] = TIMER_ENABLE |
							PRL_T_TCPC_TX_TIMEOUT;
		break;
	case PRL_SINK_TX:
		prl_sink_tx_timer[port] = TIMER_ENABLE | PRL_T_SINK_TX;
		break;
	case PRL_PS_HARD_RESET:
		prl_ps_hard_reset_timer[port] = TIMER_ENABLE |
							PRL_T_PS_HARD_RESET;
		break;
	}
}

static void update_timers(int port)
{
	if (prl_tcpc_tx_timeout_timer[port] > TIMER_ENABLE)
		prl_tcpc_tx_timeout_timer[port]--;

	if (prl_sink_tx_timer[port] > TIMER_ENABLE)
		prl_sink_tx_timer[port]--;

	if (prl_ps_hard_reset_timer[port] > TIMER_ENABLE)
		prl_ps_hard_reset_timer[port]--;
}

static bool is_expired_timer(int port, enum timer_t timer)
{
	switch (timer) {
	case PRL_TCPC_TX_TIMEOUT:
		return (prl_tcpc_tx_timeout_timer[port] == TIMER_ENABLE);
	case PRL_SINK_TX:
		return (prl_sink_tx_timer[port] == TIMER_ENABLE);
	case PRL_PS_HARD_RESET:
		return (prl_ps_hard_reset_timer[port] == TIMER_ENABLE);
	}

	return true;
}

/* Set the protocol transmit statemachine to a new state. */
static void set_state_prl_tx(const int port,
			     const enum usb_prl_tx_state new_state)
{
	set_state(port, &prl_tx[port].ctx, &prl_tx_states[new_state]);
}

/* Get the protocol transmit statemachine's current state. */
test_export_static enum usb_prl_tx_state prl_tx_get_state(const int port)
{
	return prl_tx[port].ctx.current - &prl_tx_states[0];
}

/* Print the protocol transmit statemachine's current state. */
static void print_current_prl_tx_state(const int port)
{
	if (prl_debug_level >= DEBUG_LEVEL_3)
		CPRINTS("C%d: %s", port,
				prl_tx_state_names[prl_tx_get_state(port)]);
}

/* Set the hard reset statemachine to a new state. */
static void set_state_prl_hr(const int port,
			     const enum usb_prl_hr_state new_state)
{
	set_state(port, &prl_hr[port].ctx, &prl_hr_states[new_state]);
}

/* Get the hard reset statemachine's current state. */
enum usb_prl_hr_state prl_hr_get_state(const int port)
{
	return prl_hr[port].ctx.current - &prl_hr_states[0];
}

/* Print the hard reset statemachine's current state. */
static void print_current_prl_hr_state(const int port)
{
	if (prl_debug_level >= DEBUG_LEVEL_3)
		CPRINTS("C%d: %s", port,
				prl_hr_state_names[prl_hr_get_state(port)]);
}

void pd_transmit_complete(int port, int status)
{
	prl_tx[port].xmit_status = status;
}

void pd_execute_hard_reset(int port)
{
	/* Only allow async. function calls when state machine is running */
	if (!prl_is_running(port))
		return;

	SET_FLAG(port, PRL_FLAGS_PORT_PARTNER_HARD_RESET);
	set_state_prl_hr(port, PRL_HR_RESET_LAYER);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void prl_execute_hard_reset(int port)
{
	/* Only allow async. function calls when state machine is running */
	if (!prl_is_running(port))
		return;

	SET_FLAG(port, PRL_FLAGS_PE_HARD_RESET);
	set_state_prl_hr(port, PRL_HR_RESET_LAYER);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

int prl_is_running(int port)
{
	return local_state[port] == SM_RUN;
}

static void prl_init(int port)
{
	int i;
	const struct sm_ctx cleared = {};

	prl_flags[port] = 0;
	prl_tx[port].last_xmit_type = TCPCI_MSG_SOP;
	prl_tx[port].xmit_status = TCPC_TX_UNSET;

	for (i = 0; i < NUM_SOP_STAR_TYPES; i++) {
		prl_rx[port].msg_id[i] = -1;
		prl_tx[port].msg_id_counter[i] = 0;
	}

	init_timers(port);

	/* Clear state machines and set initial states */
	prl_tx[port].ctx = cleared;
	set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);

	prl_hr[port].ctx = cleared;
	set_state_prl_hr(port, PRL_HR_WAIT_FOR_REQUEST);
}

bool prl_is_busy(int port)
{
	return false;
}

void prl_set_debug_level(enum debug_level debug_level)
{
	if (!IS_ENABLED(CONFIG_USB_PD_DEBUG_LEVEL))
		prl_debug_level = debug_level;
}

void prl_hard_reset_complete(int port)
{
	SET_FLAG(port, PRL_FLAGS_HARD_RESET_COMPLETE);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void prl_send_ctrl_msg(int port,
		      enum tcpci_msg_type type,
		      enum pd_ctrl_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].data_objs = 0;
	tx_emsg[port].len = 0;

	SET_FLAG(port, PRL_FLAGS_MSG_XMIT);

	task_wake(PD_PORT_TO_TASK_ID(port));
}

void prl_send_data_msg(int port,
		      enum tcpci_msg_type type,
		      enum pd_data_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;

	prl_copy_msg_to_buffer(port);
	SET_FLAG(port, PRL_FLAGS_MSG_XMIT);

	task_wake(PD_PORT_TO_TASK_ID(port));
}

void prl_set_default_pd_revision(int port)
{
	/*
	 * Initialize to highest revision supported. If the port or cable
	 * partner doesn't support this revision, the Protocol Engine will
	 * lower this value to the revision supported by the partner.
	 */
	pdmsg[port].rev[TCPCI_MSG_SOP] = PD_REVISION;
	pdmsg[port].rev[TCPCI_MSG_SOP_PRIME] = PD_REVISION;
	pdmsg[port].rev[TCPCI_MSG_SOP_PRIME_PRIME] = PD_REVISION;
	pdmsg[port].rev[TCPCI_MSG_SOP_DEBUG_PRIME] = PD_REVISION;
	pdmsg[port].rev[TCPCI_MSG_SOP_DEBUG_PRIME_PRIME] = PD_REVISION;
}

void prl_reset_soft(int port)
{
	/* Do not change negotiated PD Revision Specification level */
	local_state[port] = SM_INIT;

	/* Ensure we process the reset quickly */
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void prl_run(int port, int evt, int en)
{
	switch (local_state[port]) {
	case SM_PAUSED:
		if (!en)
			break;
		/* fall through */
	case SM_INIT:
		prl_init(port);
		local_state[port] = SM_RUN;
		/* fall through */
	case SM_RUN:
		if (!en) {
			tcpm_set_rx_enable(port, 0);
			local_state[port] = SM_PAUSED;
			break;
		}

		update_timers(port);
		/* Run Protocol Layer Hard Reset state machine */
		run_state(port, &prl_hr[port].ctx);

		/*
		 * If the Hard Reset state machine is active, then there is no
		 * need to execute any other PRL state machines. When the hard
		 * reset is complete, all PRL state machines will have been
		 * reset.
		 */
		if (prl_hr_get_state(port) == PRL_HR_WAIT_FOR_REQUEST) {

			/* Run Protocol Layer Message Reception */
			prl_rx_wait_for_phy_message(port, evt);

			/* Run Protocol Layer Message Tx state machine */
			run_state(port, &prl_tx[port].ctx);
		}
		break;
	}
}

void prl_set_rev(int port, enum tcpci_msg_type type,
						enum pd_rev_type rev)
{
	/* We only store revisions for SOP* types. */
	ASSERT(type < NUM_SOP_STAR_TYPES);

	pdmsg[port].rev[type] = rev;
}

enum pd_rev_type prl_get_rev(int port, enum tcpci_msg_type type)
{
	/* We only store revisions for SOP* types. */
	ASSERT(type < NUM_SOP_STAR_TYPES);

	return pdmsg[port].rev[type];
}

static void prl_copy_msg_to_buffer(int port)
{
	/*
	 * Control Messages will have a length of 0 and
	 * no need to spend time with the tx_chk_buf
	 * for this path
	 */
	if (tx_emsg[port].len == 0) {
		pdmsg[port].data_objs = 0;
		return;
	}

	/*
	 * Make sure the Policy Engine isn't sending
	 * more than CHK_BUF_SIZE_BYTES. If so,
	 * truncate len. This will surely send a
	 * malformed packet resulting in the port
	 * partner soft\hard resetting us.
	 */
	if (tx_emsg[port].len > CHK_BUF_SIZE_BYTES)
		tx_emsg[port].len = CHK_BUF_SIZE_BYTES;

	/* Copy message to chunked buffer */
	memset((uint8_t *)pdmsg[port].tx_chk_buf, 0, CHK_BUF_SIZE_BYTES);
	memcpy((uint8_t *)pdmsg[port].tx_chk_buf, (uint8_t *)tx_emsg[port].buf,
		tx_emsg[port].len);
	/*
	 * Pad length to 4-byte boundary and
	 * convert to number of 32-bit objects.
	 * Since the value is shifted right by 2,
	 * no need to explicitly clear the lower
	 * 2-bits.
	 */
	pdmsg[port].data_objs = (tx_emsg[port].len + 3) >> 2;
}

/* Common Protocol Layer Message Transmission */
static void prl_tx_phy_layer_reset_entry(const int port)
{
	print_current_prl_tx_state(port);

	/* Note: can't clear PHY messages due to TCPC architecture */
	/* Enable communications*/
	tcpm_set_rx_enable(port, pd_is_connected(port));
	set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
}

static void prl_tx_wait_for_message_request_entry(const int port)
{
	/* No phy layer response is pending */
	prl_tx[port].xmit_status = TCPC_TX_UNSET;
	print_current_prl_tx_state(port);
}

static void prl_tx_wait_for_message_request_run(const int port)
{
	/* Handle non Rev 3.0 or subsequent messages in AMS sequence */
	if (CHK_FLAG(port, PRL_FLAGS_MSG_XMIT)) {
		CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);
		/*
		 * Soft Reset Message Message pending
		 */
		if ((pdmsg[port].msg_type == PD_CTRL_SOFT_RESET) &&
						(tx_emsg[port].len == 0)) {
			set_state_prl_tx(port, PRL_TX_LAYER_RESET_FOR_TRANSMIT);
		}
		/*
		 * Message pending (except Soft Reset)
		 */
		else {
			/* NOTE: PRL_TX_Construct_Message State embedded here */
			prl_tx_construct_message(port);
			set_state_prl_tx(port, PRL_TX_WAIT_FOR_PHY_RESPONSE);
		}

		return;
	}
}

static void increment_msgid_counter(int port)
{
	/* If the last message wasn't an SOP* message, no need to increment */
	if (prl_tx[port].last_xmit_type >= NUM_SOP_STAR_TYPES)
		return;

	prl_tx[port].msg_id_counter[prl_tx[port].last_xmit_type] =
		(prl_tx[port].msg_id_counter[prl_tx[port].last_xmit_type] + 1) &
		PD_MESSAGE_ID_COUNT;
}

/*
 * PrlTxDiscard
 */
static void prl_tx_discard_message_entry(const int port)
{
	print_current_prl_tx_state(port);

	/*
	 * Discard queued message
	 * Note: We differ from spec here, which allows us to not discard on
	 * incoming SOP' or SOP''.  However this would get the TCH out of sync.
	 *
	 * prl_tx will be set to this state following message reception in
	 * prl_rx. So this path will be entered following each rx message. If
	 * this state is entered, and there is either a message from the PE
	 * pending, or if a message was passed to the phy and there is either no
	 * response yet, or it was discarded in the phy layer, then a tx message
	 * discard event has been detected.
	 */
	if (CHK_FLAG(port, PRL_FLAGS_MSG_XMIT) ||
	    prl_tx[port].xmit_status == TCPC_TX_WAIT ||
	    prl_tx[port].xmit_status == TCPC_TX_COMPLETE_DISCARDED) {
		CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);
		increment_msgid_counter(port);
		pe_report_discard(port);
	}

	set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);
}

/*
 * PrlTxLayerResetForTransmit
 */
static void prl_tx_layer_reset_for_transmit_entry(const int port)
{
	print_current_prl_tx_state(port);

	if (pdmsg[port].xmit_type < NUM_SOP_STAR_TYPES) {
		/*
		 * This state is only used during soft resets. Reset only the
		 * matching message type.
		 *
		 * From section 6.3.13 Soft Reset Message in the USB PD 3.0
		 * v2.0 spec, Soft_Reset Message Shall be targeted at a
		 * specific entity depending on the type of SOP* Packet used.
		 */
		prl_tx[port].msg_id_counter[pdmsg[port].xmit_type] = 0;

		/*
		 * From section 6.11.2.3.2, the MessageID should be cleared
		 * from the PRL_Rx_Layer_Reset_for_Receive state. However, we
		 * don't implement a full state machine for PRL RX states so
		 * clear the MessageID here.
		 */
		prl_rx[port].msg_id[pdmsg[port].xmit_type] = -1;
	}
}

static void prl_tx_layer_reset_for_transmit_run(const int port)
{
	/* NOTE: PRL_Tx_Construct_Message State embedded here */
	prl_tx_construct_message(port);
	set_state_prl_tx(port, PRL_TX_WAIT_FOR_PHY_RESPONSE);
}

static uint32_t get_sop_star_header(const int port)
{
	const int is_sop_packet = pdmsg[port].xmit_type == TCPCI_MSG_SOP;
	int ext;

	ext = 0;

	/* SOP vs SOP'/SOP" headers are different. Replace fields as needed */
	return PD_HEADER(
		pdmsg[port].msg_type,
		is_sop_packet ?
			pd_get_power_role(port) : tc_get_cable_plug(port),
		is_sop_packet ?
			pd_get_data_role(port) : 0,
		prl_tx[port].msg_id_counter[pdmsg[port].xmit_type],
		pdmsg[port].data_objs,
		pdmsg[port].rev[pdmsg[port].xmit_type],
		ext);
}

static void prl_tx_construct_message(const int port)
{
	/* The header is unused for hard reset, etc. */
	const uint32_t header = pdmsg[port].xmit_type < NUM_SOP_STAR_TYPES ?
		get_sop_star_header(port) : 0;

	/* Save SOP* so the correct msg_id_counter can be incremented */
	prl_tx[port].last_xmit_type = pdmsg[port].xmit_type;

	/* Indicate that a tx message is being passed to the phy layer */
	prl_tx[port].xmit_status = TCPC_TX_WAIT;
	/*
	 * PRL_FLAGS_TX_COMPLETE could be set if this function is called before
	 * the Policy Engine is informed of the previous transmission. Clear the
	 * flag so that this message can be sent.
	 */
	CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);

	/*
	 * Pass message to PHY Layer. It handles retries in hardware as the EC
	 * cannot handle the required timing ~ 1ms (tReceive + tRetry).
	 *
	 * Note if we ever start sending large, extendend messages, then we
	 * should not retry those messages. We do not support that and probably
	 * never will (since we support chunking).
	 */
	tcpm_transmit(port, pdmsg[port].xmit_type, header,
		      pdmsg[port].tx_chk_buf);
}

/*
 * PrlTxWaitForPhyResponse
 */
static void prl_tx_wait_for_phy_response_entry(const int port)
{
	print_current_prl_tx_state(port);

	start_timer(port, PRL_TCPC_TX_TIMEOUT);
}

static void prl_tx_wait_for_phy_response_run(const int port)
{
	/* Wait until TX is complete */

	/*
	 * NOTE: The TCPC will set xmit_status to TCPC_TX_COMPLETE_DISCARDED
	 *       when a GoodCRC containing an incorrect MessageID is received.
	 *       This condition satisfies the PRL_Tx_Match_MessageID state
	 *       requirement.
	 */

	if (prl_tx[port].xmit_status == TCPC_TX_COMPLETE_SUCCESS) {
		/* NOTE: PRL_TX_Message_Sent State embedded here. */
		/* Increment messageId counter */
		increment_msgid_counter(port);

		/* Inform Policy Engine Message was sent */
		pe_message_sent(port);

		/*
		 * This event reduces the time of informing the policy engine of
		 * the transmission by one state machine cycle
		 */
		task_wake(PD_PORT_TO_TASK_ID(port));
		set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
	} else if (is_expired_timer(port, PRL_TCPC_TX_TIMEOUT) ||
		   prl_tx[port].xmit_status == TCPC_TX_COMPLETE_FAILED) {
		/*
		 * NOTE: PRL_Tx_Transmission_Error State embedded
		 * here.
		 */

		/* Report Error To Policy Engine */
		pe_report_error(port, ERR_TCH_XMIT,
				prl_tx[port].last_xmit_type);

		/* Increment message id counter */
		increment_msgid_counter(port);
		set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
	}
}

static void prl_tx_wait_for_phy_response_exit(const int port)
{
	stop_timer(port, PRL_TCPC_TX_TIMEOUT);
}

/* Source Protocol Layer Message Transmission */
/*
 * PrlTxSrcPending
 */
static void prl_tx_src_pending_entry(const int port)
{
	print_current_prl_tx_state(port);

	/* Start SinkTxTimer */
	start_timer(port, PRL_SINK_TX);
}

static void prl_tx_src_pending_run(const int port)
{
	if (is_expired_timer(port, PRL_SINK_TX)) {
		/*
		 * We clear the pending XMIT flag here right before we send so
		 * we can detect if we discarded this message or not
		 */
		CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);

		/*
		 * Soft Reset Message pending &
		 * SinkTxTimer timeout
		 */
		if ((tx_emsg[port].len == 0) &&
			(pdmsg[port].msg_type == PD_CTRL_SOFT_RESET)) {
			set_state_prl_tx(port, PRL_TX_LAYER_RESET_FOR_TRANSMIT);
		}
		/* Message pending (except Soft Reset) &
		 * SinkTxTimer timeout
		 */
		else {
			prl_tx_construct_message(port);
			set_state_prl_tx(port, PRL_TX_WAIT_FOR_PHY_RESPONSE);
		}

		return;
	}
}

static void prl_tx_src_pending_exit(int port)
{
	stop_timer(port, PRL_SINK_TX);
}

/*
 * PrlTxSnkPending
 */
static void prl_tx_snk_pending_entry(const int port)
{
	print_current_prl_tx_state(port);
}

static void prl_tx_snk_pending_run(const int port)
{
	bool start_tx = false;
	enum tcpc_cc_voltage_status cc1, cc2;

	tcpm_get_cc(port, &cc1, &cc2);
	start_tx = (cc1 == TYPEC_CC_VOLT_RP_3_0 ||
		    cc2 == TYPEC_CC_VOLT_RP_3_0);

	if (start_tx) {
		/*
		 * We clear the pending XMIT flag here right before we send so
		 * we can detect if we discarded this message or not
		 */
		CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);

		/*
		 * Soft Reset Message Message pending &
		 * Rp = SinkTxOk
		 */
		if ((pdmsg[port].msg_type == PD_CTRL_SOFT_RESET) &&
					(tx_emsg[port].len == 0)) {
			set_state_prl_tx(port, PRL_TX_LAYER_RESET_FOR_TRANSMIT);
		}
		/*
		 * Message pending (except Soft Reset) &
		 * Rp = SinkTxOk
		 */
		else {
			prl_tx_construct_message(port);
			set_state_prl_tx(port, PRL_TX_WAIT_FOR_PHY_RESPONSE);
		}
		return;
	}
}

/* Hard Reset Operation */
void prl_hr_send_msg_to_phy(const int port)
{
	/* Header is not used for hard reset */
	const uint32_t header = 0;

	pdmsg[port].xmit_type = TCPCI_MSG_TX_HARD_RESET;

	/*
	 * These flags could be set if this function is called before the
	 * Policy Engine is informed of the previous transmission. Clear the
	 * flags so that this message can be sent.
	 */
	prl_tx[port].xmit_status = TCPC_TX_UNSET;
	CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);

	/* Pass message to PHY Layer */
	tcpm_transmit(port, pdmsg[port].xmit_type, header,
		      pdmsg[port].tx_chk_buf);
}

static void prl_hr_wait_for_request_entry(const int port)
{
	print_current_prl_hr_state(port);

	CLR_FLAG(port, HARD_RESET_FLAGS);
}

static void prl_hr_wait_for_request_run(const int port)
{
	if (CHK_FLAG(port, PRL_FLAGS_PE_HARD_RESET |
				PRL_FLAGS_PORT_PARTNER_HARD_RESET))
		set_state_prl_hr(port, PRL_HR_RESET_LAYER);
}

/*
 * PrlHrResetLayer
 */
static void prl_hr_reset_layer_entry(const int port)
{
	int i;

	print_current_prl_hr_state(port);

	CLR_FLAG(port, PDMSG_FLAGS);

	/* Hard reset resets messageIDCounters for all TX types */
	for (i = 0; i < NUM_SOP_STAR_TYPES; i++) {
		prl_rx[port].msg_id[i] = -1;
		prl_tx[port].msg_id_counter[i] = 0;
	}

	/*
	 * PD r3.0 v2.0, ss6.2.1.1.5:
	 * After a physical or logical (USB Type-C Error Recovery) Attach, a
	 * Port discovers the common Specification Revision level between itself
	 * and its Port Partner and/or the Cable Plug(s), and uses this
	 * Specification Revision level until a Detach, Hard Reset or Error
	 * Recovery happens.
	 *
	 * This covers the Hard Reset case.
	 */
	prl_set_default_pd_revision(port);

	/* Inform the AP of Hard Reset */
	if (IS_ENABLED(CONFIG_USB_PD_HOST_CMD))
		pd_notify_event(port, PD_STATUS_EVENT_HARD_RESET);

	/*
	 * Protocol Layer message transmission transitions to
	 * PRL_Tx_Wait_For_Message_Request state.
	 */
	set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
}

static void prl_hr_reset_layer_run(const int port)
{
	/*
	 * Protocol Layer reset Complete &
	 * Hard Reset was initiated by Policy Engine
	 */
	if (CHK_FLAG(port, PRL_FLAGS_PE_HARD_RESET)) {
		/*
		 * Request PHY to perform a Hard Reset. Note
		 * PRL_HR_Request_Reset state is embedded here.
		 */
		prl_hr_send_msg_to_phy(port);
		set_state_prl_hr(port, PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE);
	}
	/*
	 * Protocol Layer reset complete &
	 * Hard Reset was initiated by Port Partner
	 */
	else {
		/* Inform Policy Engine of the Hard Reset */
		pe_got_hard_reset(port);
		set_state_prl_hr(port, PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE);
	}
}

/*
 * PrlHrWaitForPhyHardResetComplete
 */
static void prl_hr_wait_for_phy_hard_reset_complete_entry(const int port)
{
	print_current_prl_hr_state(port);

	/* Start HardResetCompleteTimer */
	start_timer(port, PRL_PS_HARD_RESET);
}

static void prl_hr_wait_for_phy_hard_reset_complete_run(const int port)
{
	/*
	 * Wait for hard reset from PHY
	 * or timeout
	 */
	if (CHK_FLAG(port, PRL_FLAGS_TX_COMPLETE) ||
		is_expired_timer(port, PRL_PS_HARD_RESET)) {
		/* PRL_HR_PHY_Hard_Reset_Requested */

		/* Inform Policy Engine Hard Reset was sent */
		pe_hard_reset_sent(port);
		set_state_prl_hr(port, PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE);

		return;
	}
}

static void prl_hr_wait_for_phy_hard_reset_complete_exit(int port)
{
	stop_timer(port, PRL_PS_HARD_RESET);
}

/*
 * PrlHrWaitForPeHardResetComplete
 */
static void prl_hr_wait_for_pe_hard_reset_complete_entry(const int port)
{
	print_current_prl_hr_state(port);
}

static void prl_hr_wait_for_pe_hard_reset_complete_run(const int port)
{
	/*
	 * Wait for Hard Reset complete indication from Policy Engine
	 */
	if (CHK_FLAG(port, PRL_FLAGS_HARD_RESET_COMPLETE))
		set_state_prl_hr(port, PRL_HR_WAIT_FOR_REQUEST);
}

static void prl_hr_wait_for_pe_hard_reset_complete_exit(const int port)
{
	/* Exit from Hard Reset */
	set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);
}

static void copy_chunk_to_ext(int port)
{
	/* Calculate number of bytes */
	pdmsg[port].num_bytes_received =
				(PD_HEADER_CNT(rx_emsg[port].header) * 4);

	/* Copy chunk into extended message */
	memcpy((uint8_t *)rx_emsg[port].buf, (uint8_t *)pdmsg[port].rx_chk_buf,
		pdmsg[port].num_bytes_received);

	/* Set extended message length */
	rx_emsg[port].len = pdmsg[port].num_bytes_received;
}

/*
 * Protocol Layer Message Reception State Machine
 */
static void prl_rx_wait_for_phy_message(const int port, int evt)
{
	uint32_t header;
	uint8_t type;
	uint8_t cnt;
	int8_t msid;

	/* If we don't have any message, just stop processing now. */
	if (!tcpm_has_pending_message(port) ||
	    tcpm_dequeue_message(port, pdmsg[port].rx_chk_buf, &header))
		return;

	rx_emsg[port].header = header;
	type = PD_HEADER_TYPE(header);
	cnt = PD_HEADER_CNT(header);
	msid = PD_HEADER_ID(header);
	prl_rx[port].sop = PD_HEADER_GET_SOP(header);

	/* Make sure an incorrect count doesn't overflow the chunk buffer */
	if (cnt > CHK_BUF_SIZE)
		cnt = CHK_BUF_SIZE;

	/* dump received packet content (only dump ping at debug level MAX) */
	if ((prl_debug_level >= DEBUG_LEVEL_2 && type != PD_CTRL_PING) ||
		prl_debug_level >= DEBUG_LEVEL_3) {
		int p;

		ccprintf("C%d: RECV %04x/%d ", port, header, cnt);
		for (p = 0; p < cnt; p++)
			ccprintf("[%d]%08x ", p, pdmsg[port].rx_chk_buf[p]);
		ccprintf("\n");
	}

	/*
	 * Ignore messages sent to the cable from our
	 * port partner if we aren't Vconn powered device.
	 */
	if (!IS_ENABLED(CONFIG_USB_CTVPD) &&
	    !IS_ENABLED(CONFIG_USB_VPD) &&
	    PD_HEADER_GET_SOP(header) != TCPCI_MSG_SOP &&
	    PD_HEADER_PROLE(header) == PD_PLUG_FROM_DFP_UFP)
		return;

	/* Handle incoming soft reset as special case */
	if (cnt == 0 && type == PD_CTRL_SOFT_RESET) {
		/* Clear MessageIdCounter */
		prl_tx[port].msg_id_counter[prl_rx[port].sop] = 0;
		/* Clear stored MessageID value */
		prl_rx[port].msg_id[prl_rx[port].sop] = -1;

		/* Soft Reset occurred */
		set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);

		/*
		 * Inform Policy Engine of Soft Reset. Note perform this after
		 * performing the protocol layer reset, otherwise we will lose
		 * the PE's outgoing ACCEPT message to the soft reset.
		 */
		pe_got_soft_reset(port);

		return;
	}

	/*
	 * Ignore if this is a duplicate message. Stop processing.
	 */
	if (prl_rx[port].msg_id[prl_rx[port].sop] == msid)
		return;

	/*
	 * Discard any pending tx message if this is
	 * not a ping message (length must be checked to verify this is a
	 * control message, rather than data)
	 */
	if ((cnt > 0) || (type != PD_CTRL_PING)) {
		/*
		 * Note: Spec dictates that we always go into
		 * PRL_Tx_Discard_Message upon receivng a message.  However, due
		 * to our TCPC architecture we may be receiving a transmit
		 * complete at the same time as a response so only do this if a
		 * message is pending.
		 */
		if (prl_tx[port].xmit_status != TCPC_TX_COMPLETE_SUCCESS ||
		    CHK_FLAG(port, PRL_FLAGS_MSG_XMIT))
			set_state_prl_tx(port, PRL_TX_DISCARD_MESSAGE);
	}

	/* Store Message Id */
	prl_rx[port].msg_id[prl_rx[port].sop] = msid;

	/* Copy chunk to extended buffer */
	copy_chunk_to_ext(port);
	/* Send message to Policy Engine */
	pe_message_received(port);

	task_wake(PD_PORT_TO_TASK_ID(port));
}

/* All necessary Protocol Transmit States (Section 6.11.2.2) */
static __const_data const struct usb_state prl_tx_states[] = {
	[PRL_TX_PHY_LAYER_RESET] = {
		.entry  = prl_tx_phy_layer_reset_entry,
	},
	[PRL_TX_WAIT_FOR_MESSAGE_REQUEST] = {
		.entry  = prl_tx_wait_for_message_request_entry,
		.run    = prl_tx_wait_for_message_request_run,
	},
	[PRL_TX_LAYER_RESET_FOR_TRANSMIT] = {
		.entry  = prl_tx_layer_reset_for_transmit_entry,
		.run    = prl_tx_layer_reset_for_transmit_run,
	},
	[PRL_TX_WAIT_FOR_PHY_RESPONSE] = {
		.entry  = prl_tx_wait_for_phy_response_entry,
		.run    = prl_tx_wait_for_phy_response_run,
		.exit   = prl_tx_wait_for_phy_response_exit,
	},
	[PRL_TX_SRC_PENDING] = {
		.entry  = prl_tx_src_pending_entry,
		.run    = prl_tx_src_pending_run,
		.exit	= prl_tx_src_pending_exit,
	},
	[PRL_TX_SNK_PENDING] = {
		.entry  = prl_tx_snk_pending_entry,
		.run    = prl_tx_snk_pending_run,
	},
	[PRL_TX_DISCARD_MESSAGE] = {
		.entry  = prl_tx_discard_message_entry,
	},
};

/* All necessary Protocol Hard Reset States (Section 6.11.2.4) */
static __const_data const struct usb_state prl_hr_states[] = {
	[PRL_HR_WAIT_FOR_REQUEST] = {
		.entry  = prl_hr_wait_for_request_entry,
		.run    = prl_hr_wait_for_request_run,
	},
	[PRL_HR_RESET_LAYER] = {
		.entry  = prl_hr_reset_layer_entry,
		.run    = prl_hr_reset_layer_run,
	},
	[PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE] = {
		.entry  = prl_hr_wait_for_phy_hard_reset_complete_entry,
		.run    = prl_hr_wait_for_phy_hard_reset_complete_run,
		.exit	= prl_hr_wait_for_phy_hard_reset_complete_exit,
	},
	[PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE] = {
		.entry  = prl_hr_wait_for_pe_hard_reset_complete_entry,
		.run    = prl_hr_wait_for_pe_hard_reset_complete_run,
		.exit   = prl_hr_wait_for_pe_hard_reset_complete_exit,
	},
};
