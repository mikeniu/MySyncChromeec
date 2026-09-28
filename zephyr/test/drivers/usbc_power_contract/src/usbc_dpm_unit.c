/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "ec_commands.h"
#include "mock_usbc_power_contract.h"
#include "power.h"
#include "power_button.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"
#include "usb_common.h"
#include "usb_dp_alt_mode.h"
#include "usb_mode.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_timer.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tbt_alt_mode.h"
#include "usb_tc_sm.h"

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

enum usb_dpm_state {
	DPM_WAITING,
	DPM_DFP_READY,
	DPM_UFP_READY,
	DPM_DATA_RESET,
};

enum usb_dpm_state get_state_dpm(const int port);

static uint32_t test_src_pdos[] = {
	PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED),
};

static int custom_fake_dpm_get_source_pdo(const uint32_t **src_pdo,
					  const int port)
{
	if (src_pdo)
		*src_pdo = test_src_pdos;
	return ARRAY_SIZE(test_src_pdos);
}

static void usbc_dpm_unit_before(void *data)
{
	helper_reset_usbc_power_contract_fakes();
	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		tc_pause_event_loop(i);
		dpm_run(i, 0, 1);
		dpm_init(i);
		tc_set_data_role(i, PD_ROLE_DISCONNECTED);
		tc_set_power_role(i, PD_ROLE_SINK);
		dpm_remove_sink(i);
		dpm_remove_source(i);
		dpm_mode_exit_complete(i);
		dpm_clear_vdm_request(i);
	}
	pd_set_bist_share_mode(0);
	test_set_chipset_to_s0();
}

static void usbc_dpm_unit_after(void *data)
{
	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		dpm_init(i);
		tc_set_data_role(i, PD_ROLE_DISCONNECTED);
		tc_set_power_role(i, PD_ROLE_SINK);
		dpm_remove_sink(i);
		dpm_remove_source(i);
		dpm_mode_exit_complete(i);
		dpm_clear_vdm_request(i);
		tc_start_event_loop(i);
	}
	pd_set_bist_share_mode(0);
}

ZTEST_SUITE(usbc_dpm_unit, drivers_predicate_post_main, NULL,
	    usbc_dpm_unit_before, usbc_dpm_unit_after, NULL);

ZTEST(usbc_dpm_unit, test_dpm_init_and_state_management)
{
	dpm_set_debug_level(DEBUG_LEVEL_2);
	dpm_init(0);
	zassert_equal(get_state_dpm(0), DPM_WAITING);

	dpm_set_debug_level(DEBUG_LEVEL_1);
	dpm_set_pe_ready(0, true);
	dpm_set_pe_ready(0, false);
	dpm_set_mode_exit_request(0);
	dpm_mode_exit_complete(0);
	dpm_data_reset_complete(0);
}

ZTEST(usbc_dpm_unit, test_dpm_vdm_request_flow)
{
	uint32_t vdo[VDO_MAX_SIZE + 1] = { 0 };

	/* Count == 0 is invalid */
	zassert_equal(pd_request_vdm(0, vdo, 0, TCPCI_MSG_SOP),
		      EC_RES_INVALID_PARAM);

	/* Count > VDO_MAX_SIZE is invalid */
	zassert_equal(pd_request_vdm(0, vdo, VDO_MAX_SIZE + 1, TCPCI_MSG_SOP),
		      EC_RES_INVALID_PARAM);

	/* SVDM Attention with count > PD_ATTENTION_MAX_VDO is invalid */
	vdo[0] = VDO(USB_SID_PD, 1, CMD_ATTENTION);
	zassert_equal(pd_request_vdm(0, vdo, PD_ATTENTION_MAX_VDO + 1,
				     TCPCI_MSG_SOP),
		      EC_RES_INVALID_PARAM);

	/* Valid standard request */
	vdo[0] = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT);
	vdo[1] = 0;
	zassert_equal(pd_request_vdm(0, vdo, 2, TCPCI_MSG_SOP), EC_RES_SUCCESS);
	zassert_true(dpm_check_vdm_request(0));

	/* Second request while one pending returns EC_RES_BUSY */
	zassert_equal(pd_request_vdm(0, vdo, 2, TCPCI_MSG_SOP), EC_RES_BUSY);

	/* Clear request */
	dpm_clear_vdm_request(0);
	zassert_false(dpm_check_vdm_request(0));

	/* Valid Attention request */
	vdo[0] = VDO(USB_SID_PD, 1, CMD_ATTENTION);
	zassert_equal(pd_request_vdm(0, vdo, 2, TCPCI_MSG_SOP), EC_RES_SUCCESS);
	zassert_true(dpm_check_vdm_request(0));
	dpm_clear_vdm_request(0);
}

ZTEST(usbc_dpm_unit, test_dpm_vdm_acked_and_naked)
{
	uint32_t vdm_dp[2] = { VDO(USB_SID_DISPLAYPORT, 1, CMD_DISCOVER_MODES),
			       0 };
	uint32_t vdm_intel[2] = { VDO(USB_VID_INTEL, 1, CMD_DISCOVER_MODES),
				  0 };
	uint32_t vdm_unexp[2] = { VDO(0x1234, 1, CMD_DISCOVER_MODES), 0 };

	/* ACK dispatch */
	dpm_vdm_acked(0, TCPCI_MSG_SOP, 2, vdm_dp);
	dpm_vdm_acked(0, TCPCI_MSG_SOP, 2, vdm_intel);
	dpm_vdm_acked(0, TCPCI_MSG_SOP, 2, vdm_unexp);

	/* NAK dispatch */
	dpm_vdm_naked(0, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT, CMD_DISCOVER_MODES,
		      0);
	dpm_vdm_naked(0, TCPCI_MSG_SOP, USB_VID_INTEL, CMD_DISCOVER_MODES, 0);
	dpm_vdm_naked(0, TCPCI_MSG_SOP, 0x1234, CMD_DISCOVER_MODES, 0);
}

ZTEST(usbc_dpm_unit, test_dpm_alt_mode_queries_and_sysjump)
{
	uint32_t payload_dp[2] = { VDO(USB_SID_DISPLAYPORT, 1, CMD_ATTENTION),
				   0 };
	uint32_t payload_other[2] = { VDO(0x1234, 1, CMD_ATTENTION), 0 };

	zassert_equal(pd_alt_mode(0, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT), -1);
	zassert_equal(pd_alt_mode(0, TCPCI_MSG_SOP, USB_VID_INTEL), -1);
	zassert_equal(pd_alt_mode(0, TCPCI_MSG_SOP, 0x1234), -1);

	zassert_equal(pd_dfp_exit_mode(0, TCPCI_MSG_SOP, 0, 0), 0);
	zassert_equal(
		pd_dfp_exit_mode(0, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT, 0), 0);

	pd_prepare_sysjump();
	notify_sysjump_ready();
	zassert_true(board_is_tbt_usb4_port(0));

	dfp_consume_attention(0, payload_dp);
	dfp_consume_attention(0, payload_other);
	dpm_notify_attention(0, 2, payload_dp);
}

ZTEST(usbc_dpm_unit, test_dpm_request_enter_mode)
{
	zassert_equal(pd_request_enter_mode(board_get_usb_pd_port_count(),
					    TYPEC_MODE_DP),
		      EC_RES_INVALID_PARAM);
	zassert_equal(pd_request_enter_mode(0, (enum typec_mode)0xff),
		      EC_RES_INVALID_PARAM);

	zassert_equal(pd_request_enter_mode(0, TYPEC_MODE_DP), EC_RES_SUCCESS);
	zassert_equal(pd_request_enter_mode(0, TYPEC_MODE_DP), EC_RES_BUSY);

	dpm_init(0);
	zassert_equal(pd_request_enter_mode(0, TYPEC_MODE_TBT), EC_RES_SUCCESS);

	dpm_init(0);
	zassert_equal(pd_request_enter_mode(0, TYPEC_MODE_USB4),
		      EC_RES_SUCCESS);
}

ZTEST(usbc_dpm_unit, test_dpm_pd_button_state_machine)
{
	/* Non-extended alert is ignored */
	dpm_handle_alert(0, 0);

	tc_set_data_role(0, PD_ROLE_DFP);
	dpm_set_pe_ready(0, true);
	dpm_run(0, 0, 1);
	/* First transition triggers initial VCONN swap to WAITING; next enters
	 * DFP_READY */
	if (get_state_dpm(0) == DPM_WAITING) {
		dpm_set_pe_ready(0, true);
		dpm_run(0, 0, 1);
	}
	zassert_equal(get_state_dpm(0), DPM_DFP_READY, "state was %d",
		      get_state_dpm(0));

	/* Simultaneous press and release clears both flags */
	dpm_handle_alert(0, ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS);
	dpm_handle_alert(0,
			 ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE);
	dpm_run(0, 0, 1);

	/* Press then re-press while pressed */
	dpm_handle_alert(0, ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS);
	dpm_run(0, 0, 1);
	dpm_handle_alert(0, ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS);
	dpm_run(0, 0, 1);

	/* Long press timer expiration returns to idle */
	pd_timer_enable(0, DPM_TIMER_PD_BUTTON_LONG_PRESS, 0);
	dpm_run(0, 0, 1);

	/* Short press when system is OFF wakes system */
	test_set_chipset_to_g3();
	dpm_handle_alert(0, ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS);
	dpm_run(0, 0, 1);
	dpm_handle_alert(0,
			 ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE);
	dpm_run(0, 0, 1);

	/* Short press when system is ON simulates power button press */
	test_set_chipset_to_s0();
	dpm_handle_alert(0, ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS);
	dpm_run(0, 0, 1);
	pd_timer_enable(0, DPM_TIMER_PD_BUTTON_SHORT_PRESS, 10000000);
	dpm_handle_alert(0,
			 ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE);
	dpm_run(0, 0, 1);

	/* Long press when system is ON forces shutdown */
	test_set_chipset_to_s0();
	dpm_handle_alert(0, ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS);
	dpm_run(0, 0, 1);
	pd_timer_enable(0, DPM_TIMER_PD_BUTTON_SHORT_PRESS, 0);
	dpm_handle_alert(0,
			 ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE);
	dpm_run(0, 0, 1);
}

ZTEST(usbc_dpm_unit, test_dpm_source_power_balancing)
{
	/* Non-fixed PDO */
	dpm_evaluate_sink_fixed_pdo(0, PDO_VAR(5000, 9000, 3000));

	/* Non-5V fixed PDO */
	dpm_evaluate_sink_fixed_pdo(0, PDO_FIXED(9000, 3000, 0));

	/* Source role <= 1500mA */
	tc_set_power_role(0, PD_ROLE_SOURCE);
	dpm_evaluate_sink_fixed_pdo(0, PDO_FIXED(5000, 1500, 0));
	zassert_equal(dpm_get_source_current(0), 1500);

	/* Source role > 1500mA claims 3.0A */
	dpm_evaluate_sink_fixed_pdo(0, PDO_FIXED(5000, 3000, 0));
	zassert_equal(dpm_get_source_current(0), 3000);

	/* Remove sink restores default Rp */
	dpm_remove_sink(0);
	zassert_equal(dpm_get_source_current(0), 1500);

	/* Remove sink again is a no-op */
	dpm_remove_sink(0);

	/* Add non-PD sink claims 3.0A */
	dpm_add_non_pd_sink(0);
	zassert_equal(dpm_get_source_current(0), 3000);
	dpm_remove_sink(0);
	zassert_equal(dpm_get_source_current(0), 1500);

	/* Sink role FRS evaluation */
	tc_set_power_role(0, PD_ROLE_SINK);
	zassert_equal(dpm_get_source_current(0), 0);

	/* Rev 2.0 ignored */
	prl_set_rev(0, TCPCI_MSG_SOP, PD_REV20);
	dpm_evaluate_sink_fixed_pdo(
		0,
		PDO_FIXED(5000, 1000,
			  PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_1A5_AT_5V));

	/* Rev 3.0 without dual role ignored */
	prl_set_rev(0, TCPCI_MSG_SOP, PD_REV30);
	dpm_evaluate_sink_fixed_pdo(0, PDO_FIXED(5000, 1000,
						 PDO_FIXED_FRS_CURR_1A5_AT_5V));

	/* Rev 3.0 with FRS default */
	dpm_evaluate_sink_fixed_pdo(
		0, PDO_FIXED(5000, 1000,
			     PDO_FIXED_DUAL_ROLE |
				     PDO_FIXED_FRS_CURR_DFLT_USB_POWER));

	/* Rev 3.0 with FRS 1A5 */
	dpm_evaluate_sink_fixed_pdo(
		0,
		PDO_FIXED(5000, 1000,
			  PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_1A5_AT_5V));

	/* Rev 3.0 with FRS 3A0 */
	dpm_evaluate_sink_fixed_pdo(
		0,
		PDO_FIXED(5000, 1000,
			  PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_3A0_AT_5V));

	/* Remove source */
	dpm_remove_source(0);
	dpm_remove_source(0);

	/* Contention: Non-PD sink downgraded for PD sink */
	tc_set_power_role(0, PD_ROLE_SOURCE);
	tc_set_power_role(1, PD_ROLE_SOURCE);
	dpm_add_non_pd_sink(0);
	zassert_equal(dpm_get_source_current(0), 3000);
	dpm_evaluate_sink_fixed_pdo(1, PDO_FIXED(5000, 3000, 0));
	zassert_equal(dpm_get_source_current(0), 1500);
	dpm_remove_sink(0);
	dpm_remove_sink(1);

	/* Contention: FRS downgraded for PD sink */
	tc_set_power_role(0, PD_ROLE_SINK);
	prl_set_rev(0, TCPCI_MSG_SOP, PD_REV30);
	dpm_evaluate_sink_fixed_pdo(
		0,
		PDO_FIXED(5000, 1000,
			  PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_3A0_AT_5V));
	tc_set_power_role(1, PD_ROLE_SOURCE);
	dpm_evaluate_sink_fixed_pdo(1, PDO_FIXED(5000, 3000, 0));
	dpm_remove_source(0);
	dpm_remove_sink(1);

	/* Contention: Non-PD downgraded for FRS */
	tc_set_power_role(0, PD_ROLE_SOURCE);
	dpm_add_non_pd_sink(0);
	tc_set_power_role(1, PD_ROLE_SINK);
	prl_set_rev(1, TCPCI_MSG_SOP, PD_REV30);
	dpm_evaluate_sink_fixed_pdo(
		1,
		PDO_FIXED(5000, 1000,
			  PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_3A0_AT_5V));
	dpm_remove_sink(0);
	dpm_remove_source(1);
}

ZTEST(usbc_dpm_unit, test_dpm_bist_shared_mode)
{
	zassert_false(pd_get_bist_share_mode());

	/* Entry when not source is ignored */
	tc_set_power_role(0, PD_ROLE_SINK);
	dpm_bist_shared_mode_enter(0);
	zassert_false(pd_get_bist_share_mode());

	/* Entry when source */
	tc_set_power_role(0, PD_ROLE_SOURCE);
	dpm_bist_shared_mode_enter(0);
	zassert_true(pd_get_bist_share_mode());
	zassert_equal(dpm_get_source_current(0), 3000);

	/* Exit */
	dpm_bist_shared_mode_exit(0);
	zassert_false(pd_get_bist_share_mode());

	/* Exit when not active is ignored */
	dpm_bist_shared_mode_exit(0);

	/* Direct get/set API */
	zassert_ok(pd_set_bist_share_mode(1));
	zassert_true(pd_get_bist_share_mode());
	zassert_ok(pd_set_bist_share_mode(0));
	zassert_false(pd_get_bist_share_mode());
}

ZTEST(usbc_dpm_unit, test_dpm_status_msg_and_power_indicators)
{
	uint8_t msg[16];
	uint32_t len = 0;

	/* S0 state */
	test_set_chipset_to_s0();
	zassert_ok(dpm_get_status_msg(0, msg, &len));
	zassert_equal(len, 7);
	zassert_equal(board_get_pd_sdb_power_indicator(PD_SDB_POWER_STATE_S0),
		      PD_SDB_POWER_INDICATOR_ON);

	/* S3 state */
	test_set_chipset_to_power_level(POWER_S3);
	zassert_ok(dpm_get_status_msg(0, msg, &len));
	zassert_equal(board_get_pd_sdb_power_indicator(PD_SDB_POWER_STATE_S3),
		      PD_SDB_POWER_INDICATOR_BLINKING);

	/* S5 state */
	test_set_chipset_to_power_level(POWER_S5);
	zassert_ok(dpm_get_status_msg(0, msg, &len));
	zassert_equal(board_get_pd_sdb_power_indicator(PD_SDB_POWER_STATE_S5),
		      PD_SDB_POWER_INDICATOR_OFF);

	/* G3 state */
	test_set_chipset_to_g3();
	zassert_ok(dpm_get_status_msg(0, msg, &len));
	zassert_equal(board_get_pd_sdb_power_indicator(PD_SDB_POWER_STATE_G3),
		      PD_SDB_POWER_INDICATOR_OFF);

	/* Standby and Unsupported indicators */
	zassert_equal(board_get_pd_sdb_power_indicator(
			      PD_SDB_POWER_STATE_MODERN_STANDBY),
		      PD_SDB_POWER_INDICATOR_BLINKING);
	zassert_equal(board_get_pd_sdb_power_indicator(
			      PD_SDB_POWER_STATE_NOT_SUPPORTED),
		      PD_SDB_POWER_INDICATOR_OFF);
}

ZTEST(usbc_dpm_unit, test_dpm_source_info_msg)
{
	/* Fallback when 0 PDOs available */
	dpm_get_source_pdo_fake.return_val = 0;
	union sido info0 = dpm_get_source_info_msg(0);
	zassert_equal(info0.sido1.port_reported_pdp, 0);

	/* Offered PDOs */
	dpm_get_source_pdo_fake.custom_fake = custom_fake_dpm_get_source_pdo;
	union sido info1 = dpm_get_source_info_msg(0);
	zassert_equal(info1.sido1.port_reported_pdp, 15);
	zassert_equal(info1.sido1.port_present_pdp, 15);

	/* Port 1 claims 3.0A so Port 0 present PDP falls back to reported PDP
	 */
	tc_set_power_role(1, PD_ROLE_SOURCE);
	dpm_add_non_pd_sink(1);
	union sido info2 = dpm_get_source_info_msg(0);
	zassert_equal(info2.sido1.port_present_pdp,
		      info2.sido1.port_reported_pdp);
	dpm_remove_sink(1);
}

ZTEST(usbc_dpm_unit, test_dpm_state_machine_run)
{
	/* Paused execution */
	dpm_run(0, 0, 0);
	dpm_run(0, 0, 0);

	/* Enable into DPM_WAITING */
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_WAITING);

	/* While !PE_READY stays DPM_WAITING */
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_WAITING);

	/* Transition to DPM_UFP_READY */
	tc_set_data_role(0, PD_ROLE_UFP);
	dpm_set_pe_ready(0, true);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_UFP_READY);

	/* Return to DPM_WAITING if !PE_READY */
	dpm_set_pe_ready(0, false);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_WAITING);

	/* Re-enter DPM_UFP_READY and test enter_any clear */
	dpm_set_pe_ready(0, true);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_UFP_READY);
	pd_request_enter_mode(0, TYPEC_MODE_DP);
	dpm_run(0, 0, 1);

	/* VDM request sends and transitions to DPM_WAITING */
	uint32_t vdo[2] = { VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT), 0 };
	pd_request_vdm(0, vdo, 2, TCPCI_MSG_SOP);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_WAITING);

	/* Transition to DPM_DFP_READY */
	tc_set_data_role(0, PD_ROLE_DFP);
	dpm_set_pe_ready(0, true);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_DFP_READY);

	/* Return to DPM_WAITING if !PE_READY */
	dpm_set_pe_ready(0, false);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_WAITING);

	/* Re-enter DPM_DFP_READY */
	dpm_set_pe_ready(0, true);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_DFP_READY);

	/* VDM request sends and transitions to DPM_WAITING */
	pd_request_vdm(0, vdo, 2, TCPCI_MSG_SOP);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_WAITING);

	/* Data reset transition flow */
	tc_set_data_role(0, PD_ROLE_DFP);
	dpm_set_pe_ready(0, true);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_DFP_READY);

	pd_request_enter_mode(0, TYPEC_MODE_DP);
	pd_set_svids_discovery(0, TCPCI_MSG_SOP, PD_DISC_COMPLETE);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_DATA_RESET);

	/* In DPM_DATA_RESET, wait for DATA_RESET_DONE */
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_DATA_RESET);

	dpm_data_reset_complete(0);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_DFP_READY);
	/* Run in DFP_READY with DATA_RESET_DONE to trigger mode entry handling
	 */
	dpm_run(0, 0, 1);

	/* Exit mode flow */
	dpm_set_mode_exit_request(0);
	dpm_run(0, 0, 1);
	dpm_data_reset_complete(0);
	dpm_run(0, 0, 1);
	/* Run again in DFP_READY to execute exit mode message and clear exit
	 * request */
	dpm_run(0, 0, 1);

	/* CPU off and discovery incomplete checks */
	test_set_chipset_to_g3();
	dpm_run(0, 0, 1);
	test_set_chipset_to_s0();
	pd_set_svids_discovery(0, TCPCI_MSG_SOP, PD_DISC_NEEDED);
	dpm_run(0, 0, 1);
	pd_set_svids_discovery(0, TCPCI_MSG_SOP, PD_DISC_COMPLETE);
	dpm_run(0, 0, 1);

	/* VCONN swap flow */
	pd_request_vconn_swap(0);
	dpm_run(0, 0, 1);
	zassert_equal(get_state_dpm(0), DPM_WAITING);
}
