/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_typec_drp_acc_trysrc_shim.h"

void tc_attach_wait_src_run(int port)
{
	int cc1, cc2;

	tcpm_get_cc(port, &cc1, &cc2);

	if (cc_is_snk_dbg_acc(cc1, cc2)) {
		/*
		 * Debug Accessory (Rd/Rd) handling on non-DTS port.
		 */
		if (board_is_dts_port(port)) {
			set_state_tc(port, TC_ATTACHED_SRC);
		} else if (tc_get_drp_state(port) == PD_DRP_FORCE_SINK) {
			set_state_tc(port, TC_UNATTACHED_SNK);
		} else {
			return;
		}
		return;
	}

	if (cc_is_at_least_one_rd(cc1, cc2)) {
		if (pd_check_vbus_level(port, VBUS_SAFE0V) &&
		    pd_timer_is_expired(port, PD_TIMER_CC_DEBOUNCE)) {
			set_state_tc(port, TC_ATTACHED_SRC);
		}
	}
}
