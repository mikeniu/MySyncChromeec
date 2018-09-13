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
#include "tcpm.h"
#include "util.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usb_tc_sm.h"
#include "usb_emsg.h"
#include "usb_sm.h"
#include "version.h"

/* PD counter definitions */
#define PD_MESSAGE_ID_COUNT 7
#define N_RETRY_COUNT 2

#define RCH_OBJ(port)	(SM_OBJ(rch[port]))
#define TCH_OBJ(port)	(SM_OBJ(tch[port]))
#define PRL_TX_OBJ(port)   (SM_OBJ(prl_tx[port]))
#define PRL_HR_OBJ(port)   (SM_OBJ(prl_hr[port]))

enum l_state {
	PRL_INIT,
	PRL_RUN,
	PRL_PAUSED
};

static enum l_state local_state = PRL_INIT;

/* Chunked Rx State Machine Object */
static struct rx_chunked {
	/* struct sm_obj must be first. */
	struct sm_obj obj;
	/* state machine flags */
	uint32_t sm_flags;
	/* protocol timer */
	uint64_t chunk_sender_response_timer;
} rch[CONFIG_USB_PD_PORT_COUNT];

/* Chunked Tx State Machine Object */
static struct tx_chunked {
	/* struct sm_obj must be first. */
	struct sm_obj obj;
	/* state machine flags */
	uint32_t sm_flags;
	/* protocol timer */
	uint64_t chunk_sender_request_timer;
} tch[CONFIG_USB_PD_PORT_COUNT];

/* Message Reception State Machine Object */
static struct protocol_layer_rx {
	/* struct sm_obj must be first. */
	struct sm_obj obj;
	/* state machine flags */
	uint32_t sm_flags;
	/* message id counter */
	int msg_id;
} prl_rx[CONFIG_USB_PD_PORT_COUNT];

/* Message Transmission State Machine Object */
static struct protocol_layer_tx {
	/* struct sm_obj must be first. */
	struct sm_obj obj;
	/* state machine flags */
	uint32_t sm_flags;
	/* protocol timer */
	uint64_t sink_tx_timer;
	/* message id counter */
	uint32_t msg_id_counter;
	/* message retry counter */
	uint32_t retry_counter;
	/* transmit status */
	int xmit_status;
} prl_tx[CONFIG_USB_PD_PORT_COUNT];

/* Hard Reset State Machine Object */
static struct protocol_hard_reset {
	/* struct sm_obj must be first. */
	struct sm_obj obj;
	/* state machine flags */
	uint32_t sm_flags;
	/* protocol timer */
	uint64_t hard_reset_complete_timer;
} prl_hr[CONFIG_USB_PD_PORT_COUNT];

/* Chunking Message Object */
static struct pd_message {
	/* message status flags */
	uint32_t status_flags;

	/* SOP* */
	enum tcpm_transmit_type xmit_type;
	/* type of message */
	uint8_t msg_type;
	/* extended message */
	uint8_t ext;

	/* Number of 32-bit objects in chk_buf */
	uint16_t data_objs;
	/* temp chunk buffer */
	uint32_t chk_buf[7];
	uint32_t chunk_number_expected;
	uint32_t num_bytes_received;
	uint32_t chunk_number_to_send;
	uint32_t send_offset;
} pdmsg[CONFIG_USB_PD_PORT_COUNT];

struct extended_msg emsg[CONFIG_USB_PD_PORT_COUNT];

/* Protocol Layer States */
/* Common Protocol Layer Message Transmission */
static unsigned int prl_tx_wait_for_message_request(int port, int sig);
static unsigned int prl_tx_layer_reset_for_transmit(int port, int sig);
static void  prl_tx_construct_message(int port);
static unsigned int prl_tx_wait_for_phy_response(int port, int sig);
static unsigned int prl_tx_src_source_tx(int port, int sig);
static unsigned int prl_tx_snk_start_ams(int port, int sig);

/* Source Protocol Layser Message Transmission */
static unsigned int prl_tx_src_pending(int port, int sig);

/* Sink Protocol Layer Message Transmission */
static unsigned int prl_tx_snk_pending(int port, int sig);

/* Protocol Layer Message Reception */
static unsigned int prl_rx_wait_for_phy_message(int port, int evt);

/* Hard Reset Operation */
static unsigned int prl_hr_reset_layer(int port, int sig);
static unsigned int prl_hr_wait_for_phy_hard_reset_complete(int port, int sig);
static unsigned int prl_hr_wait_for_pe_hard_reset_complete(int port, int sig);

/* Chunked Rx */
static unsigned int rch_wait_for_message_from_protocol_layer(int port, int sig);
static unsigned int rch_processing_extended_message(int port, int sig);
static unsigned int rch_requesting_chunk(int port, int sig);
static unsigned int rch_waiting_chunk(int port, int sig);
static unsigned int rch_report_error(int port, int sig);

/* Chunked Tx */
static unsigned int tch_wait_for_message_request_from_pe(int port, int sig);
static unsigned int tch_wait_for_transmission_complete(int port, int sig);
static unsigned int tch_construct_chunked_message(int port, int sig);
static unsigned int tch_sending_chunked_message(int port, int sig);
static unsigned int tch_wait_chunk_request(int port, int sig);
static unsigned int tch_message_received(int port, int sig);

void pd_transmit_complete(int port, int status)
{
	prl_tx[port].xmit_status = status;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_TX, 0);
}

void pd_execute_hard_reset(int port)
{
	prl_hr[port].sm_flags |= SM_FLAGS_PE_HARD_RESET;
	set_state(port, PRL_HR_OBJ(port), prl_hr_reset_layer);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_init(int port)
{
	prl_tx[port].sm_flags = 0;
	prl_rx[port].sm_flags = 0;
	tch[port].sm_flags = 0;
	rch[port].sm_flags = 0;
	pdmsg[port].status_flags = 0;
	prl_hr[port].sm_flags = 0;

	prl_rx[port].msg_id = -1;
	prl_tx[port].msg_id_counter = 0;

	init_state(port, PRL_TX_OBJ(port), prl_tx_wait_for_message_request);
	init_state(port, RCH_OBJ(port),
				rch_wait_for_message_from_protocol_layer);
	init_state(port, TCH_OBJ(port), tch_wait_for_message_request_from_pe);
	init_state(port, PRL_HR_OBJ(port),
				prl_hr_wait_for_pe_hard_reset_complete);
}

void prl_start_ams(int port)
{
	prl_tx[port].sm_flags |= SM_FLAGS_START_AMS;
}

void prl_end_ams(int port)
{
	prl_tx[port].sm_flags |= SM_FLAGS_END_AMS;
}

void prl_hard_reset_complete(int port)
{
	prl_hr[port].sm_flags |= SM_FLAGS_HARD_RESET_COMPLETE;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_send_ctrl_msg(int port,
		      enum tcpm_transmit_type type,
		      enum pd_ctrl_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].ext = 0;
	emsg[port].len = 0;
	tch[port].sm_flags |= SM_FLAGS_MSG_XMIT;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_send_data_msg(int port,
		      enum tcpm_transmit_type type,
		      enum pd_data_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].ext = 0;

	tch[port].sm_flags |= SM_FLAGS_MSG_XMIT;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_send_ext_data_msg(int port,
			  enum tcpm_transmit_type type,
			  enum pd_ext_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].ext = 1;

	tch[port].sm_flags |= SM_FLAGS_MSG_XMIT;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void protocol_layer(int port, int evt, int en)
{
	switch (local_state) {
	case PRL_INIT:
		prl_init(port);
		local_state = PRL_RUN;
		/* fall through */
	case PRL_RUN:
		if (!en) {
			local_state = PRL_PAUSED;
			break;
		}

		/* Run Protocol Layer Message Reception */
		prl_rx_wait_for_phy_message(port, evt);

		/* Run RX Chunked state machine */
		exe_state(port, RCH_OBJ(port), RUN_SIG);

		/* Run TX Chunked state machine */
		exe_state(port, TCH_OBJ(port), RUN_SIG);

		/* Run Protocol Layer Message Transmission state machine */
		exe_state(port, PRL_TX_OBJ(port), RUN_SIG);

		/* Run Protocol Layer Hard Reset state machine */
		if (prl_hr[port].sm_flags & SM_FLAGS_PE_HARD_RESET ||
			prl_hr[port].sm_flags &
					SM_FLAGS_PORT_PARTNER_HARD_RESET) {
			exe_state(port, PRL_HR_OBJ(port), RUN_SIG);
		}
		break;
	case PRL_PAUSED:
		if (en)
			local_state = PRL_INIT;
		break;
	}
}

/* Common Protocol Layer Message Transmission */
static unsigned int prl_tx_wait_for_message_request(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Reset RetryCounter */
		prl_tx[port].retry_counter = 0;
		break;
	case RUN_SIG:
		if (prl_tx[port].sm_flags & SM_FLAGS_START_AMS ||
			prl_tx[port].sm_flags & SM_FLAGS_END_AMS) {
			if (tc_get_power_role(port) == PD_ROLE_SOURCE) {
				/*
				 * Start of AMS notification received from
				 * Policy Engine
				 */
				if (prl_tx[port].sm_flags &
							SM_FLAGS_START_AMS) {
					prl_tx[port].sm_flags &=
							~SM_FLAGS_START_AMS;
					SET_STATE(port, PRL_TX_OBJ(port),
						prl_tx_src_source_tx);
				}
				/*
				 * End of AMS notification received from
				 * Policy Engine
				 */
				else if (prl_tx[port].sm_flags &
							    SM_FLAGS_END_AMS) {
					prl_tx[port].sm_flags &=
							~SM_FLAGS_END_AMS;
					/* Set Rp = SinkTxOk */
					tcpm_select_rp_value(port, SINK_TX_OK);
					tcpm_set_cc(port, TYPEC_CC_RP);

					prl_tx[port].retry_counter = 0;
					prl_tx[port].sm_flags = 0;
				}
			} else {
				if (prl_tx[port].sm_flags &
							SM_FLAGS_START_AMS) {
					prl_tx[port].sm_flags &=
							~SM_FLAGS_START_AMS;
					/*
					 * First Message in AMS notification
					 * received from Policy Engine.
					 */
					SET_STATE(port, PRL_TX_OBJ(port),
							  prl_tx_snk_start_ams);
				}
			}
		} else if (prl_tx[port].sm_flags & SM_FLAGS_MSG_XMIT) {
			prl_tx[port].sm_flags &= ~SM_FLAGS_MSG_XMIT;
			/*
			 * Soft Reset Message Message pending
			 */
			if ((pdmsg[port].msg_type == PD_CTRL_SOFT_RESET) &&
							(emsg[port].len == 0)) {
				SET_STATE(port, PRL_TX_OBJ(port),
					prl_tx_layer_reset_for_transmit);
			}
			/*
			 * Message pending (except Soft Reset)
			 */
			else {
				prl_tx_construct_message(port);
				SET_STATE(port, PRL_TX_OBJ(port),
					prl_tx_wait_for_phy_response);
			}
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static void increment_msgid_counter(int port)
{
	prl_tx[port].msg_id_counter = (prl_tx[port].msg_id_counter + 1) &
		PD_MESSAGE_ID_COUNT;
}


static unsigned int prl_tx_discard_message(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Increment msgidCounter */
		increment_msgid_counter(port);
		SET_STATE(port, PRL_TX_OBJ(port),
			prl_tx_wait_for_message_request);
		break;
	case RUN_SIG:

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int prl_tx_src_source_tx(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Set Rp = SinkTxNG */
		tcpm_select_rp_value(port, SINK_TX_NG);
		tcpm_set_cc(port, TYPEC_CC_RP);
		break;
	case RUN_SIG:
		if (prl_tx[port].sm_flags & SM_FLAGS_MSG_XMIT) {
			prl_tx[port].sm_flags &= ~SM_FLAGS_MSG_XMIT;

			SET_STATE(port, PRL_TX_OBJ(port), prl_tx_src_pending);
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int prl_tx_snk_start_ams(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		break;
	case RUN_SIG:
		if (prl_tx[port].sm_flags & SM_FLAGS_MSG_XMIT) {
			prl_tx[port].sm_flags &= ~SM_FLAGS_MSG_XMIT;

			SET_STATE(port, PRL_TX_OBJ(port), prl_tx_snk_pending);
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int prl_tx_layer_reset_for_transmit(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Reset MessageIdCounter */
		prl_tx[port].msg_id_counter = 0;
		break;
	case RUN_SIG:
		/* Construct message and Pass to PHY Layer */
		prl_tx_construct_message(port);
		SET_STATE(port, PRL_TX_OBJ(port), prl_tx_wait_for_phy_response);
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static void prl_tx_construct_message(int port)
{
	uint32_t header = PD_HEADER(pdmsg[port].msg_type,
				     tc_get_power_role(port),
				     tc_get_data_role(port),
				     prl_tx[port].msg_id_counter,
				     pdmsg[port].data_objs,
				     emsg[port].rev,
				     pdmsg[port].ext
				    );
	/* Pass message to PHY Layer */
	tcpm_transmit(port, pdmsg[port].xmit_type, header,
						pdmsg[port].chk_buf);
}

static unsigned int prl_tx_wait_for_phy_response(int port, int sig)
{
	int evt;

	switch (sig) {
	case ENTRY_SIG:
		break;
	case RUN_SIG:
		/* Wait until TX is complete */
		evt = task_wait_event_mask(PD_EVENT_TX, PD_T_TCPC_TX_TIMEOUT);

		if (evt & TASK_EVENT_TIMER || prl_tx[port].xmit_status !=
						TCPC_TX_COMPLETE_SUCCESS) {
			/* Increment check RetryCounter */
			prl_tx[port].retry_counter++;
			/*
			 * (RetryCounter > nRetryCount) |
			 * Large Extended Message
			 */
			if (prl_tx[port].retry_counter > N_RETRY_COUNT ||
				    (pdmsg[port].ext &&
				    PD_EXT_HEADER_DATA_SIZE(GET_EXT_HEADER(
				    pdmsg[port].chk_buf[0]) > 26))) {
				/* Inform policy engine of error */
				pe_report_error(port, ERR_PRL_TX);
				/* Increment messageid counter */
				increment_msgid_counter(port);

				SET_STATE(port, PRL_TX_OBJ(port),
					prl_tx_wait_for_message_request);
				STATE_RETURN;
			}

			/* Try to resend the message. */
			prl_tx_construct_message(port);
			STATE_RETURN;
		}

		if (evt & PD_EVENT_TX) {
			/* Increment messageId counter */
			increment_msgid_counter(port);
			/* Inform Policy Engine Message was sent */
			pdmsg[port].status_flags |= SM_FLAGS_TX_COMPLETE;
			SET_STATE(port, PRL_TX_OBJ(port),
				prl_tx_wait_for_message_request);
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

/* Source Protocol Layer Message Transmission */
static unsigned int prl_tx_src_pending(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Start SinkTxTimer */
		prl_tx[port].sink_tx_timer = get_time().val + PD_T_SINK_TX;
		break;
	case RUN_SIG:
		if (get_time().val > prl_tx[port].sink_tx_timer) {
			/*
			 * Soft Reset Message pending &
			 * SinkTxTimer timeout
			 */
			if ((emsg[port].len == 0) &&
				(pdmsg[port].msg_type == PD_CTRL_SOFT_RESET)) {
				SET_STATE(port, PRL_TX_OBJ(port),
					prl_tx_layer_reset_for_transmit);
			}
			/* Message pending (except Soft Reset) &
			 * SinkTxTimer timeout
			 */
			else {
				prl_tx_construct_message(port);
				SET_STATE(port, PRL_TX_OBJ(port),
						prl_tx_wait_for_phy_response);
			}
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}
	return 0;
}

static unsigned int prl_tx_snk_pending(int port, int sig)
{
	int cc1;
	int cc2;

	switch (sig) {
	case ENTRY_SIG:
		break;
	case RUN_SIG:
		tcpm_get_cc(port, &cc1, &cc2);
		if (cc1 == TYPEC_CC_VOLT_SNK_3_0 ||
					cc2 == TYPEC_CC_VOLT_SNK_3_0) {
			/*
			 * Soft Reset Message Message pending &
			 * Rp = SinkTxOk
			 */
			if ((pdmsg[port].msg_type == PD_CTRL_SOFT_RESET) &&
						(emsg[port].len == 0)) {
				SET_STATE(port, PRL_TX_OBJ(port),
					prl_tx_layer_reset_for_transmit);
			}
			/*
			 * Message pending (except Soft Reset) &
			 * Rp = SinkTxOk
			 */
			else {
				prl_tx_construct_message(port);
				SET_STATE(port, PRL_TX_OBJ(port),
						prl_tx_wait_for_phy_response);
			}
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}
	return 0;
}

/* Hard Reset Operation */
static unsigned int prl_hr_reset_layer(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* reset messageIDCounter */
		prl_tx[port].msg_id_counter = 0;
		/*
		 * Protocol Layer message transmission transitions to
		 * PRL_Tx_Wait_For_Message_Request state.
		 */
		SET_STATE(port, PRL_TX_OBJ(port),
			prl_tx_wait_for_message_request);

		break;
	case RUN_SIG:
		/*
		 * Protocol Layer reset Complete &
		 * (Hard Reset was initiated by Policy Engine
		 */
		if (prl_hr[port].sm_flags & SM_FLAGS_PE_HARD_RESET) {
			/* Request PHY to perform a Hard Reset */
			prl_send_ctrl_msg(port, TCPC_TX_HARD_RESET, 0);
			SET_STATE(port, PRL_HR_OBJ(port),
				prl_hr_wait_for_phy_hard_reset_complete);
		}
		/*
		 * Protocol Layer reset complete &
		 * (Hard Reset was initiated by Port Partner
		 */
		else {
			/* Inform Policy Engine of the Hard Reset */
			pe_got_hard_reset(port);
			SET_STATE(port, PRL_HR_OBJ(port),
				prl_hr_wait_for_pe_hard_reset_complete);
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}
	return 0;
}

static unsigned int prl_hr_wait_for_phy_hard_reset_complete(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Start HardResetCompleteTimer */
		prl_hr[port].hard_reset_complete_timer =
				get_time().val + PD_T_PS_HARD_RESET;
		break;
	case RUN_SIG:
		/*
		 * Wait for hard reset from PHY
		 * or timeout
		 */
		if ((pdmsg[port].status_flags & SM_FLAGS_TX_COMPLETE) ||
		    (get_time().val > prl_hr[port].hard_reset_complete_timer)) {
			/* PRL_HR_PHY_Hard_Reset_Requested */
			{
				/* Inform Policy Engine Hard Reset was sent */
				pe_hard_reset_sent(port);
				SET_STATE(port, PRL_HR_OBJ(port),
					prl_hr_wait_for_pe_hard_reset_complete);
			}
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int prl_hr_wait_for_pe_hard_reset_complete(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		break;
	case RUN_SIG:
		/*
		 * Wait for Hard Reset complete indication from Policy Engine
		 */
		if (prl_hr[port].sm_flags & SM_FLAGS_HARD_RESET_COMPLETE)
			prl_hr[port].sm_flags = 0;
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static void copy_chunk_to_ext(int port)
{
	/* Calculate number of bytes */
	pdmsg[port].num_bytes_received = (PD_HEADER_CNT(emsg[port].header) * 4);

	/* Copy chunk into extended message */
	memcpy((uint8_t *)emsg[port].buf, (uint8_t *)pdmsg[port].chk_buf,
		pdmsg[port].num_bytes_received);

	/* Set extended message length */
	emsg[port].len = pdmsg[port].num_bytes_received;
}

/*
 * Chunked Rx State Machine
 */
static unsigned int rch_wait_for_message_from_protocol_layer(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Clear Extended Rx Buffer */
		/* Clear About flag */
		pdmsg[port].status_flags &= ~SM_FLAGS_ABORT;
		/* All Messages are chunked */
		rch[port].sm_flags = SM_FLAGS_CHUNKING;
		break;
	case RUN_SIG:
		if (rch[port].sm_flags & SM_FLAGS_MSG_RECEIVED) {
			rch[port].sm_flags &= ~SM_FLAGS_MSG_RECEIVED;

			/* Is this an extended message? */
			if (PD_HEADER_EXT(emsg[port].header)) {
				uint16_t exhdr =
					GET_EXT_HEADER(*pdmsg[port].chk_buf);
				uint8_t chunked = PD_EXT_HEADER_CHUNKED(exhdr);

				/*
				 * Received Extended Message &
				 * (Chunking = 1 & Chunked = 1)
				 */
				if ((rch[port].sm_flags & SM_FLAGS_CHUNKING) &&
						chunked) {
					SET_STATE(port, RCH_OBJ(port),
					       rch_processing_extended_message);
				}
				/*
				 * (Received Extended Message &
				 * (Chunking = 0 & Chunked = 0))
				 */
				else if (!(rch[port].sm_flags &
					      SM_FLAGS_CHUNKING) && !chunked) {
					/* Copy chunk to extended buffer */
					copy_chunk_to_ext(port);
					/* Pass Message to Policy Engine */
					pe_pass_up_message(port);
					/* Clear Extended Rx Buffer */
					/* Clear Abort Flag */
					pdmsg[port].status_flags &=
								~SM_FLAGS_ABORT;
					/* All Messages are chunked */
					rch[port].sm_flags = SM_FLAGS_CHUNKING;
				}
				/*
				 * Chunked != Chunking
				 */
				else {
					SET_STATE(port, RCH_OBJ(port),
							rch_report_error);
				}
			}
			/*
			 * Received Non-Extended Message
			 */
			else {
				/* Copy chunk to extended buffer */
				copy_chunk_to_ext(port);
				/* Pass Message to Policy Engine */
				pe_pass_up_message(port);
				/* Clear Extended Rx Buffer */
				/* Clear Abort Flag */
				pdmsg[port].status_flags &= ~SM_FLAGS_ABORT;
				/* All Messages are chunked */
				rch[port].sm_flags = SM_FLAGS_CHUNKING;
			}
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}
	return 0;
}

static unsigned int rch_processing_extended_message(int port, int sig)
{
	uint32_t header = emsg[port].header;
	uint16_t exhdr = GET_EXT_HEADER(pdmsg[port].chk_buf[0]);
	uint8_t chunk_num = PD_EXT_HEADER_CHUNK_NUM(exhdr);
	uint32_t data_size = PD_EXT_HEADER_DATA_SIZE(exhdr);
	uint8_t byte_num = (PD_HEADER_CNT(header) * 4) - 2;

	switch (sig) {
	case ENTRY_SIG:
		/*
		 * If first chunk:
		 *   Set Chunk_number_expected = 0 and
		 *   Num_Bytes_Received = 0
		 */
		if (chunk_num == 0) {
			pdmsg[port].chunk_number_expected = 0;
			pdmsg[port].num_bytes_received = 0;
			pdmsg[port].msg_type = PD_HEADER_TYPE(header);
		}
		break;
	case RUN_SIG:
		/*
		 * Abort Flag Set
		 */
		if (pdmsg[port].status_flags & SM_FLAGS_ABORT) {
			SET_STATE(port, RCH_OBJ(port),
				rch_wait_for_message_from_protocol_layer);
		}
		/*
		 * If expected Chunk Number:
		 *   Append data to Extended_Message_Buffer
		 *   Increment Chunk_number_Expected
		 *   Adjust Num Bytes Received
		 */
		else if (chunk_num == pdmsg[port].chunk_number_expected) {
			if (byte_num < 26)
				byte_num = data_size -
					pdmsg[port].num_bytes_received;

			/* Append data */
			memcpy(((uint8_t *)emsg[port].buf +
				pdmsg[port].num_bytes_received),
				(uint8_t *)pdmsg[port].chk_buf + 2, byte_num);
			/* increment chunk number expected */
			pdmsg[port].chunk_number_expected++;
			/* adjust num bytes received */
			pdmsg[port].num_bytes_received += byte_num;

			/* Was that the last chunk? */
			if (pdmsg[port].num_bytes_received >= data_size) {
				emsg[port].len = pdmsg[port].num_bytes_received;
				 /* Pass Message to Policy Engine */
				pe_pass_up_message(port);
				SET_STATE(port, RCH_OBJ(port),
				      rch_wait_for_message_from_protocol_layer);
			}
			/*
			 * Message not Complete
			 */
			else {
				SET_STATE(port, RCH_OBJ(port),
						rch_requesting_chunk);
			}
		}
		/*
		 * Unexpected Chunk Number
		 */
		else
			SET_STATE(port, RCH_OBJ(port), rch_report_error);

	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int rch_requesting_chunk(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/*
		 * Send Chunk Request to Protocol Layer
		 * with chunk number = Chunk_Number_Expected
		 */
		pdmsg[port].chk_buf[0] = PD_EXT_HEADER(
				       pdmsg[port].chunk_number_expected,
				       1, /* Request Chunk */
				       0 /* Data Size */
				       );

		pdmsg[port].data_objs = 1;
		pdmsg[port].ext = 1;
		prl_tx[port].sm_flags |= SM_FLAGS_MSG_XMIT;
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_TX, 0);
		break;
	case RUN_SIG:
		/*
		 * Transmission Error from Protocol Layer or
		 * Message Received From Protocol Layer
		 */
		if (rch[port].sm_flags & SM_FLAGS_MSG_RECEIVED ||
				pdmsg[port].status_flags & SM_FLAGS_TX_ERROR) {
			/*
			 * Leave SM_FLAGS_MSG_RECEIVED flag set. It'll be
			 * cleared in rch_report_error state
			 */
			SET_STATE(port, RCH_OBJ(port), rch_report_error);
		}
		/*
		 * Message Transmitted received from Protocol Layer
		 */
		else if (pdmsg[port].status_flags & SM_FLAGS_TX_COMPLETE) {
			pdmsg[port].status_flags &= ~SM_FLAGS_TX_COMPLETE;
			SET_STATE(port, RCH_OBJ(port), rch_waiting_chunk);
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int rch_waiting_chunk(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/*
		 * Start ChunkSenderResponseTimer
		 */
		rch[port].chunk_sender_response_timer =
			get_time().val + PD_T_CHUNK_SENDER_RESPONSE;
		break;
	case RUN_SIG:
		if ((rch[port].sm_flags & SM_FLAGS_MSG_RECEIVED)) {
			/*
			 * Leave SM_FLAGS_MSG_RECEIVED flag set. It'll be
			 * cleared in rch_report_error state
			 */

			if (PD_HEADER_EXT(emsg[port].header)) {
				uint16_t exhdr =
					GET_EXT_HEADER(pdmsg[port].chk_buf[0]);
				/*
				 * Other Message Received from Protocol Layer
				 */
				if (PD_EXT_HEADER_REQ_CHUNK(exhdr) ||
						!PD_EXT_HEADER_CHUNKED(exhdr)) {
					SET_STATE(port, RCH_OBJ(port),
							rch_report_error);
				}
				/*
				 * Chunk response Received from Protocol Layer
				 */
				else {
					SET_STATE(port, RCH_OBJ(port),
					rch_processing_extended_message);
				}
			}
		}
		/*
		 * ChunkSenderResponseTimer Timeout
		 */
		else if (get_time().val >
				rch[port].chunk_sender_response_timer) {
			SET_STATE(port, RCH_OBJ(port), rch_report_error);
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int rch_report_error(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/*
		 * If the state was entered because a message was received,
		 * this message is passed to the Policy Engine.
		 */
		if (rch[port].sm_flags & SM_FLAGS_MSG_RECEIVED) {
			rch[port].sm_flags &= ~SM_FLAGS_MSG_RECEIVED;

			/* Copy chunk to extended buffer */
			copy_chunk_to_ext(port);
			/* Pass Message to Policy Engine */
			pe_pass_up_message(port);
			/* Report error */
			pe_report_error(port, ERR_RCH_MSG_REC);
		} else {
			/* Report error */
			pe_report_error(port, ERR_RCH_CHUNKED);
		}
		break;
	case RUN_SIG:
		SET_STATE(port, RCH_OBJ(port),
			rch_wait_for_message_from_protocol_layer);
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

/*
 * Chunked Tx State Machine
 */
static unsigned int tch_wait_for_message_request_from_pe(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Clear Abort Flag */
		pdmsg[port].status_flags &= ~SM_FLAGS_ABORT;

		/* All Messages are chunked */
		tch[port].sm_flags = SM_FLAGS_CHUNKING;
		break;
	case RUN_SIG:
		/*
		 * Any message received and not in state TCH_Wait_Chunk_Request
		 */
		if (tch[port].sm_flags & SM_FLAGS_MSG_RECEIVED) {
			tch[port].sm_flags &= ~SM_FLAGS_MSG_RECEIVED;
			SET_STATE(port, TCH_OBJ(port), tch_message_received);
		} else if (tch[port].sm_flags & SM_FLAGS_MSG_XMIT) {
			tch[port].sm_flags &= ~SM_FLAGS_MSG_XMIT;
			/*
			 * Rx Chunking State !=
			 * RCH_Wait_For_Message_From_Protocol_Layer &
			 * Abort Supported
			 *
			 * Discard the Message
			 */
			if (rch[port].obj.task_state !=
				     rch_wait_for_message_from_protocol_layer) {
				/* Report Error To Policy Engine */
				pe_report_error(port, ERR_TCH_XMIT);
				/* Clear Abort Flag */
				pdmsg[port].status_flags &= ~SM_FLAGS_ABORT;
				/* All Messages are chunked */
				tch[port].sm_flags = SM_FLAGS_CHUNKING;
			} else {
				/*
				 * Extended Message Request &
				 * Chunking
				 */
				if (pdmsg[port].ext &&
				     (tch[port].sm_flags & SM_FLAGS_CHUNKING)) {
					pdmsg[port].send_offset = 0;
					pdmsg[port].chunk_number_to_send = 0;
					SET_STATE(port, TCH_OBJ(port),
						tch_construct_chunked_message);
				}
				/*
				 * Non-Extended Message Request
				 */
				else {
					/* Copy message to chunked buffer */
					memcpy((uint8_t *)pdmsg[port].chk_buf,
						(uint8_t *)emsg[port].buf,
						emsg[port].len);

					/*
					 * Pad length to 4-byte boundery and
					 * convert to number of 32-bit objects
					 */
					pdmsg[port].data_objs =
					       ((emsg[port].len + 3) & ~3) >> 2;
					/* Pass Message to Protocol Layer */
					prl_tx[port].sm_flags |=
							      SM_FLAGS_MSG_XMIT;
					SET_STATE(port, TCH_OBJ(port),
					    tch_wait_for_transmission_complete);
				}
			}
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int tch_wait_for_transmission_complete(int port, int sig)
{
	switch (sig) {
	case RUN_SIG:
		/*
		 * Any message received and not in state TCH_Wait_Chunk_Request
		 */
		if (tch[port].sm_flags & SM_FLAGS_MSG_RECEIVED) {
			tch[port].sm_flags &= ~SM_FLAGS_MSG_RECEIVED;
			SET_STATE(port, TCH_OBJ(port), tch_message_received);
			break;
		}

		/*
		 * Inform Policy Engine that Message was sent.
		 */
		if (pdmsg[port].status_flags & SM_FLAGS_TX_COMPLETE) {
			pdmsg[port].status_flags &= ~SM_FLAGS_TX_COMPLETE;
			/* Tell PE message was sent */
			pe_message_sent(port);
			SET_STATE(port, TCH_OBJ(port),
				tch_wait_for_message_request_from_pe);
		}
		/*
		 * Inform Policy Engine of Tx Error
		 */
		else if (pdmsg[port].status_flags & SM_FLAGS_TX_ERROR) {
			pdmsg[port].status_flags &= ~SM_FLAGS_TX_ERROR;
			/* Tell PE an error occurred */
			pe_report_error(port, ERR_TCH_XMIT);
			SET_STATE(port, TCH_OBJ(port),
				tch_wait_for_message_request_from_pe);
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int tch_construct_chunked_message(int port, int sig)
{
	uint16_t *ext_hdr;
	uint8_t *data;
	uint16_t num;

	switch (sig) {
	case ENTRY_SIG:
		/*
		 * Any message received and not in state TCH_Wait_Chunk_Request
		 */
		if (tch[port].sm_flags & SM_FLAGS_MSG_RECEIVED) {
			tch[port].sm_flags &= ~SM_FLAGS_MSG_RECEIVED;
			SET_STATE(port, TCH_OBJ(port), tch_message_received);
			break;
		}

		/* Prepare to copy chunk into chk_buf */

		ext_hdr = (uint16_t *)pdmsg[port].chk_buf;
		data = ((uint8_t *)pdmsg[port].chk_buf + 2);
		num = emsg[port].len - pdmsg[port].send_offset;

		/* Set the chunks extended header */
		*ext_hdr = PD_EXT_HEADER(pdmsg[port].chunk_number_to_send,
					 0, /* Chunk Request */
					 emsg[port].len);
		if (num > 26)
			num = 26;

		/* Copy the message chunk into chk_buf */
		memcpy(data, emsg[port].buf + pdmsg[port].send_offset, num);
		pdmsg[port].send_offset += num;

		/*
		 * Add in 2 bytes for extended header
		 * pad out to 4-byte boundary
		 * convert to number of 4-byte words
		 */
		pdmsg[port].data_objs = ((num + 2 + 3) & ~3) >> 2;

		/* Pass message chunk to Protocol Layer */
		prl_tx[port].sm_flags |= SM_FLAGS_MSG_XMIT;
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
		break;
	case RUN_SIG:
		if (pdmsg[port].status_flags & SM_FLAGS_ABORT) {
			SET_STATE(port, TCH_OBJ(port),
				tch_wait_for_message_request_from_pe);
		} else {
			SET_STATE(port, TCH_OBJ(port),
					tch_sending_chunked_message);
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int tch_sending_chunked_message(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		break;
	case RUN_SIG:
		/*
		 * Any message received and not in state TCH_Wait_Chunk_Request
		 */
		if (tch[port].sm_flags & SM_FLAGS_MSG_RECEIVED) {
			tch[port].sm_flags &= ~SM_FLAGS_MSG_RECEIVED;
			SET_STATE(port, TCH_OBJ(port), tch_message_received);
			break;
		}

		/*
		 * Transmission Error
		 */
		if (pdmsg[port].status_flags & SM_FLAGS_TX_ERROR) {
			pe_report_error(port, ERR_TCH_XMIT);
			SET_STATE(port, TCH_OBJ(port),
				tch_wait_for_message_request_from_pe);
		}
		/*
		 * Message Transmitted from Protocol Layer &
		 * Last Chunk
		 */
		else if (emsg[port].len == pdmsg[port].send_offset) {
			/* Tell PE message was sent */
			pe_message_sent(port);
			SET_STATE(port, TCH_OBJ(port),
				tch_wait_for_message_request_from_pe);
		}
		/*
		 * Message Transmitted from Protocol Layer &
		 * Not Last Chunk
		 */
		else
			SET_STATE(port, TCH_OBJ(port), tch_wait_chunk_request);
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int tch_wait_chunk_request(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Increment Chunk Number to Send */
		pdmsg[port].chunk_number_to_send++;
		/* Start Chunk Sender Request Timer */
		tch[port].chunk_sender_request_timer =
			get_time().val + PD_T_CHUNK_SENDER_REQUEST;
		break;
	case RUN_SIG:
		if (tch[port].sm_flags & SM_FLAGS_MSG_RECEIVED) {
			tch[port].sm_flags &= ~SM_FLAGS_MSG_RECEIVED;

			if (PD_HEADER_EXT(emsg[port].header)) {
				uint16_t exthdr;

				exthdr = GET_EXT_HEADER(pdmsg[port].chk_buf[0]);
				if (PD_EXT_HEADER_REQ_CHUNK(exthdr)) {
					/*
					 * Chunk Request Received &
					 * Chunk Number = Chunk Number to Send
					 */
					if (PD_EXT_HEADER_CHUNK_NUM(exthdr) ==
					     pdmsg[port].chunk_number_to_send) {
						SET_STATE(port, TCH_OBJ(port),
						tch_construct_chunked_message);
					}
					/*
					 * Chunk Request Received &
					 * Chunk Number != Chunk Number to Send
					 */
					else {
						pe_report_error(port,
							       ERR_TCH_CHUNKED);
						SET_STATE(port, TCH_OBJ(port),
					  tch_wait_for_message_request_from_pe);
					}
					break;
				}
			}

			/*
			 * Other message received
			 */
			SET_STATE(port, TCH_OBJ(port), tch_message_received);
		}
		/*
		 * ChunkSenderRequestTimer timeout
		 */
		else if (get_time().val >=
				tch[port].chunk_sender_request_timer) {
			/* Tell PE message was sent */
			pe_message_sent(port);
			SET_STATE(port, TCH_OBJ(port),
				tch_wait_for_message_request_from_pe);
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

static unsigned int tch_message_received(int port, int sig)
{
	switch (sig) {
	case ENTRY_SIG:
		/* Clear Extended Message Buffer */
		/* Pass message to chunked Rx */
		rch[port].sm_flags |= SM_FLAGS_MSG_RECEIVED;
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
		break;
	case RUN_SIG:
		SET_STATE(port, TCH_OBJ(port),
			tch_wait_for_message_request_from_pe);
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}

/*
 * Protocol Layer Message Reception State Machine
 */
static unsigned int prl_rx_wait_for_phy_message(int port, int evt)
{
	uint32_t header;
	uint8_t type;
	uint8_t cnt;
	int8_t msid;
	int ret;

	/* process any potential incoming message */
	if (tcpm_has_pending_message(port)) {
		ret = tcpm_dequeue_message(port, pdmsg[port].chk_buf, &header);
		if (ret == 0) {
			emsg[port].header = header;
			type = PD_HEADER_TYPE(header);
			cnt = PD_HEADER_CNT(header);
			msid = PD_HEADER_ID(header);

			if (cnt == 0 && type == PD_CTRL_SOFT_RESET) {
				/* Clear MessageIdCounter */
				prl_tx[port].msg_id_counter = 0;
				/* Clear stored MessageID value */
				prl_rx[port].msg_id = -1;
				/* Inform Policy Engine of Soft Reset */
				pe_got_soft_reset(port);

				SET_STATE(port, PRL_TX_OBJ(port),
					       prl_tx_wait_for_message_request);
			}

			/*
			 * Ignore if this is a duplicate message.
			 */
			if (prl_rx[port].msg_id != msid) {
				/*
				 * Discard any pending tx message if this is
				 * not a ping message
				 */
				if (cnt == 0 && type != PD_CTRL_PING) {
					if (prl_tx[port].obj.task_state ==
						prl_tx_src_pending ||
						prl_tx[port].obj.task_state ==
						prl_tx_snk_pending) {
						SET_STATE(port,
							PRL_TX_OBJ(port),
							prl_tx_discard_message);
					}
				}

				/* Store Message Id */
				prl_rx[port].msg_id = msid;

				/* Route the message. */
				/*
				 * Received Ping from Protocol Layer
				 */
				if (cnt == 0 && type == PD_CTRL_PING) {
					/* RTR_PING */
					emsg[port].len = 0;
					pe_pass_up_message(port);
				}
				/*
				 * Message (not Ping) Received from
				 * Protocol Layer & Doing Tx Chunks
				 */
				else if (tch[port].obj.task_state !=
					tch_wait_for_message_request_from_pe) {
					/* RTR_TX_CHUNKS */
					/*
					 * Send Message to Tx Chunk
					 * Chunk State Machine
					 */
					tch[port].sm_flags |=
							SM_FLAGS_MSG_RECEIVED;
					task_set_event(PD_PORT_TO_TASK_ID(port),
								PD_EVENT_SM, 0);
				}
				/*
				 * Message (not Ping) Received from
				 * Protocol Layer & Not Doing Tx Chunks
				 */
				else {
					/* RTR_RX_CHUNKS */
					/*
					 * Send Message to Rx
					 * Chunk State Machine
					 */
					rch[port].sm_flags |=
							SM_FLAGS_MSG_RECEIVED;
					task_set_event(PD_PORT_TO_TASK_ID(port),
								PD_EVENT_SM, 0);
				}
			}
		}
	}

	return 0;
}
