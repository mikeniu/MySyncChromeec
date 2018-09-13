/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "task.h"
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"
#include "usb_emsg.h"
#include "usb_sm.h"

enum l_state {
	PE_INIT,
	PE_RUN,
	PE_PAUSED
};

static enum l_state local_state = PE_INIT;

#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
#define VPD_OBJ(port)   (SM_OBJ(pe_vpd[port]))

static struct policy_engine_vpd {
	/* struct sm_obj must be first. */
	struct sm_obj obj;
	/* state machine flags */
	uint32_t sm_flags;
} pe_vpd[CONFIG_USB_PD_PORT_COUNT];

static unsigned int pe_vpd_request(int port, int sig);
#endif

void pe_init(int port)
{
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
	pe_vpd[port].sm_flags = 0;
	init_state(port, VPD_OBJ(port), pe_vpd_request);
#endif
}

void policy_engine(int port, int evt, int en)
{
	switch (local_state) {
	case PE_INIT:
		pe_init(port);
		local_state = PE_RUN;
		/* fall through */
	case PE_RUN:
		if (!en) {
			local_state = PE_PAUSED;
			break;
		}
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
		exe_state(port, VPD_OBJ(port), RUN_SIG);
#endif
		break;
	case PE_PAUSED:
		if (en)
			local_state = PE_INIT;
		break;
	}
}

void pe_pass_up_message(int port)
{
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
	pe_vpd[port].sm_flags |= SM_FLAGS_MSG_RECEIVED;
#endif

	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void pe_hard_reset_sent(int port)
{
	/* Do nothing */
}

void pe_got_hard_reset(int port)
{
	/* Do nothing */
}

void pe_report_error(int port, enum pe_error e)
{
	/* Do nothing */
}

void pe_got_soft_reset(int port)
{
	/* Do nothing */
}

void pe_message_sent(int port)
{
	/* Do nothing */
}

#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_VPD_CT)
static unsigned int pe_vpd_request(int port, int sig)
{
	uint32_t *payload = (uint32_t *)emsg[port].buf;
	uint16_t header = emsg[port].header;
	uint32_t vdo = payload[0];

	switch (sig) {
	case ENTRY_SIG:
		break;
	case RUN_SIG:
		if (pe_vpd[port].sm_flags & SM_FLAGS_MSG_RECEIVED) {
			pe_vpd[port].sm_flags &= ~SM_FLAGS_MSG_RECEIVED;

			/*
			 * Only support Structured VDM Discovery
			 * Identity message
			 */

			if (PD_HEADER_TYPE(header) != PD_DATA_VENDOR_DEF)
				STATE_RETURN;

			if (PD_HEADER_CNT(header) == 0)
				STATE_RETURN;

			if (!PD_VDO_SVDM(vdo))
				STATE_RETURN;

			if (PD_VDO_CMD(vdo) != CMD_DISCOVER_IDENT)
				STATE_RETURN;
#ifdef CONFIG_USB_TYPEC_VPD_CT
			/*
			 * We have a valid DISCOVER IDENTITY message.
			 * Attempt to reset support timer
			 */
			tc_reset_support_timer(port);
#endif
			/* Prepare to send ACK */

			/* VDM Header */
			payload[0] = VDO(
				USB_VID_GOOGLE,
				1, /* Structured VDM */
				VDO_SVDM_VERS(1) |
				VDO_CMDT(CMDT_RSP_ACK) |
				CMD_DISCOVER_IDENT);

			/* ID Header VDO */
			payload[1] = VDO_IDH(
				0, /* Not a USB HOST */
				0, /* Not a USB DEVICE */
				IDH_PTYPE_VPD,
				1, /* Modal Operation Supported */
				USB_VID_GOOGLE);

			/* Cert State VDO */
			payload[2] = 0;

			/* Product VDO */
			payload[3] = VDO_PRODUCT(
				CONFIG_USB_PID,
				0); /* USB bcdDevice */

			/* VPD VDO */
			payload[4] = VDO_VPD(
				VPD_HW_VERSION,
				VPD_FW_VERSION,
				VPD_MAX_VBUS_20V,
				VPD_VBUS_IMP(VPD_VBUS_IMPEDANCE),
				VPD_GND_IMP(VPD_GND_IMPEDANCE),
				VPD_CTS_SUPPORTED);

			/* 20 bytes, 5 data objects */
			emsg[port].len = 20;

			/* Use same revision */
			emsg[port].rev = PD_HEADER_REV(header);

			/* Send the ACK */
			prl_send_data_msg(port, TCPC_TX_SOP_PRIME,
						PD_DATA_VENDOR_DEF);
		}
	RUN_SIG_DONE;
	case EXIT_SIG:
		break;
	}

	return 0;
}
#endif /* CONFIG_USB_TYPEC_VPD || CONFIG_USB_TYPEC_VPD_CT */
