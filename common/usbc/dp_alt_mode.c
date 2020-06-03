/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * DisplayPort alternate mode support
 * Refer to VESA DisplayPort Alt Mode on USB Type-C Standard, version 2.0,
 * section 5.2
 */

#include <stdint.h>
#include "assert.h"
#include "usb_pd.h"
#include "usb_dp_alt_mode.h"
#include "usb_pd_tcpm.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/* The next VDM command to send for DP setup */
static int next_vdm_cmd[CONFIG_USB_PD_PORT_MAX_COUNT];

void dp_init(int port)
{
	dp_reset_next_command(port);
}

void dp_vdm_cmd_acked(int port, int cmd)
{
	if (next_vdm_cmd[port] != cmd) {
		CPRINTF("C%d: Received unexpected VDM ACK for command %d\n",
				port, cmd);
		return;
	}

	switch (cmd) {
	case CMD_ENTER_MODE:
		next_vdm_cmd[port] = CMD_DP_STATUS;
		break;
	case CMD_DP_STATUS:
		next_vdm_cmd[port] = CMD_DP_CONFIG;
		break;
	case CMD_DP_CONFIG:
		break;
	default:
		/* This should never happen */
		assert(false);
	}
}

void dp_reset_next_command(int port)
{
	CPRINTF("C%d: Resetting next command to Enter Mode\n", port);
	next_vdm_cmd[port] = CMD_ENTER_MODE;
}

int dp_setup_next_vdm(int port, int vdo_count, uint32_t *vdm)
{
	const struct svdm_amode_data *modep = pd_get_amode_data(port,
			TCPC_TX_SOP, USB_SID_DISPLAYPORT);
	int vdo_count_ret;

	if (vdo_count < VDO_MAX_SIZE)
		return -1;

	switch (next_vdm_cmd[port]) {
	case CMD_ENTER_MODE:
		/* Enter the first supported mode for DisplayPort. */
		vdm[0] = pd_dfp_enter_mode(port, TCPC_TX_SOP,
				USB_SID_DISPLAYPORT, 0);
		if (vdm[0] == 0)
			return -1;
		/* CMDT_INIT is 0, so this is a no-op */
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
		vdo_count_ret = 1;
		break;
	case CMD_DP_STATUS:
		if (!(modep && modep->opos))
			return -1;

		vdo_count_ret = modep->fx->status(port, vdm);
		if (vdo_count_ret == 0)
			return -1;
		vdm[0] |= PD_VDO_OPOS(modep->opos);
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
		break;
	case CMD_DP_CONFIG:
		if (!(modep && modep->opos))
			return -1;

		vdo_count_ret = modep->fx->config(port, vdm);
		if (vdo_count_ret == 0)
			return -1;
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
		break;
	default:
		CPRINTF("%s called with invalid next VDM command %d\n",
				__func__, next_vdm_cmd[port]);
		return -1;
	}
	return vdo_count_ret;
}
