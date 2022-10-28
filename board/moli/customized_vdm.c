/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdint.h>
#include "chipset.h"
#include "console.h"
#include "power_button.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_dpm.h"
#include "usb_pd_tcpm.h"
#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif
/* The state of the DP negotiation */
enum docking_monitor_vdm_states {
	DOCKING_MONITOR_VDM_START = 0,
	DOCKING_MONITOR_VDM_WAIT_ATTENTION,
	DOCKING_MONITOR_VDM_GET_CONFIG,
	DOCKING_MONITOR_VDM_DONE,
	DOCKING_MONITOR_VDM_STATE_COUNT
};
static enum docking_monitor_vdm_states
	docking_monitor_vdm_state[CONFIG_USB_PD_PORT_MAX_COUNT];
__override void board_docking_monitor_mode_init(int port)
{
	docking_monitor_vdm_state[port] = DOCKING_MONITOR_VDM_START;
}
__override bool board_docking_monitor_mode_entry_is_done(int port)
{
	return docking_monitor_vdm_state[port] == DOCKING_MONITOR_VDM_DONE ||
	       docking_monitor_vdm_state[port] ==
		       DOCKING_MONITOR_VDM_WAIT_ATTENTION;
}
void docking_monitor_mode_attention(int port)
{
	docking_monitor_vdm_state[port] = DOCKING_MONITOR_VDM_GET_CONFIG;
}
__override void board_docking_monitor_vdm_acked(int port,
						enum tcpci_msg_type type,
						int vdo_count, uint32_t *vdm)
{
	switch (docking_monitor_vdm_state[port]) {
	case DOCKING_MONITOR_VDM_START:
		docking_monitor_vdm_state[port] =
			DOCKING_MONITOR_VDM_WAIT_ATTENTION;
		CPRINTS("C%d: Entered docking monitor mode", port);
		break;
	case DOCKING_MONITOR_VDM_GET_CONFIG:
		docking_monitor_vdm_state[port] = DOCKING_MONITOR_VDM_DONE;
		switch ((vdm[1] & MONITOR_STATUS_MASK) >> 3) {
		case MONITOR_STATUS_OFF_TO_ON:
			CPRINTS("monitor off to on");
			if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
				chipset_power_on();
			else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
				power_button_simulate_press(200);
			}
			break;
		case MONITOR_STATUS_STANDBY_TO_OFF:
			CPRINTS("monitor standby to off");
			break;
		case MONITOR_STATUS_ON_TO_OFF:
			CPRINTS("monitor on to off");
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}
__override enum dpm_msg_setup_status
board_docking_monitor_setup_next_vdm(int port, int *vdo_count, uint32_t *vdm)
{
	int vdo_count_ret = 0;

	if (*vdo_count < VDO_MAX_SIZE)
		return MSG_SETUP_ERROR;
	switch (docking_monitor_vdm_state[port]) {
	case DOCKING_MONITOR_VDM_START:
		vdm[0] = pd_dfp_enter_mode(port, TCPCI_MSG_SOP,
					   USB_VID_USBC_MONITOR, 0);
		if (vdm[0] == 0)
			return MSG_SETUP_ERROR;
		/* CMDT_INIT is 0, so this is a no-op */
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPCI_MSG_SOP));
		vdo_count_ret = 1;
		CPRINTS("C%d: Attempting to enter docking monitor mode", port);
		break;
	case DOCKING_MONITOR_VDM_GET_CONFIG:
		vdm[0] = VDO(USB_VID_USBC_MONITOR, 1,
			     CMD_DOCKING_MONITOR_CONFIG);
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPCI_MSG_SOP));
		if (chipset_in_state(CHIPSET_STATE_ON))
			/* S0 */
			vdm[1] |= SYSTEM_STATUS_ON;
		else if (chipset_in_state(CHIPSET_STATE_SOFT_OFF))
			/* S5 */
			vdm[1] |= SYSTEM_STATUS_S5;
		else if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
			/* G3 */
			vdm[1] |= SYSTEM_STATUS_G3;
		else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			/* S3 */
			vdm[1] |= SYSTEM_STATUS_SUSPEND;
		vdo_count_ret = 2;
		break;
	default:
		break;
	}
	if (vdo_count_ret) {
		*vdo_count = vdo_count_ret;
		return MSG_SETUP_SUCCESS;
	}
	return MSG_SETUP_UNSUPPORTED;
}
__override int board_customized_vdm_response(int port, uint32_t *payload)
{
	uint8_t vdo_cmd;
	/* Extract VDM command from the VDM header */
	vdo_cmd = PD_VDO_CMD(payload[0]);
	if (vdo_cmd == CMD_DOCKING_MONITOR_ATTENTION) {
		ccprints("docking monitor attention 0x15");
		docking_monitor_mode_attention(port);
		return 1;
	}
	return 0;
}
