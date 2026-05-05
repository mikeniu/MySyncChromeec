/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef USB_TYPEC_DRP_ACC_TRYSRC_SHIM_H
#define USB_TYPEC_DRP_ACC_TRYSRC_SHIM_H

#include <stdbool.h>
#include <stdint.h>

enum usb_tc_state {
	TC_UNATTACHED_SNK = 0,
	TC_ATTACH_WAIT_SNK,
	TC_ATTACHED_SNK,
	TC_UNATTACHED_SRC,
	TC_ATTACH_WAIT_SRC,
	TC_ATTACHED_SRC,
};

enum pd_drp_state {
	PD_DRP_TOGGLE_OFF = 0,
	PD_DRP_TOGGLE_ON,
	PD_DRP_FORCE_SINK,
	PD_DRP_FORCE_SOURCE,
	PD_DRP_FREEZE,
};

enum vbus_level {
	VBUS_SAFE0V = 0,
	VBUS_PRESENT,
	VBUS_TOO_HIGH,
};

enum pd_task_timer {
	PD_TIMER_CC_DEBOUNCE = 0,
	PD_TIMER_VBUS_DEBOUNCE,
};

int tcpm_get_cc(int port, int *cc1, int *cc2);
bool cc_is_snk_dbg_acc(int cc1, int cc2);
bool cc_is_at_least_one_rd(int cc1, int cc2);
bool board_is_dts_port(int port);
void set_state_tc(int port, enum usb_tc_state new_state);
bool pd_check_vbus_level(int port, enum vbus_level level);
bool pd_timer_is_expired(int port, enum pd_task_timer timer);
enum pd_drp_state tc_get_drp_state(int port);

void tc_attach_wait_src_run(int port);

#endif /* USB_TYPEC_DRP_ACC_TRYSRC_SHIM_H */