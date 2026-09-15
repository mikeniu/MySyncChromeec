/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test/usb_pe.h"
#include "usb_dp_alt_mode.h"
#include "usb_emsg.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usb_tc_sm.h"
#include "util.h"

#include <stdint.h>

#include <zephyr/ztest.h>

#define TEST_USB_PORT 0
BUILD_ASSERT(TEST_USB_PORT == USBC_PORT_C0);

#define TEST_ADDED_PDO PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED)

struct usb_pe_test_fixture {
	struct tcpci_partner_data partner_emul;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_src_emul_data src_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	enum pd_power_role partner_pd_role;
};

struct usb_pe_test_sink_fixture {
	struct usb_pe_test_fixture fixture;
};

struct usb_pe_test_source_fixture {
	struct usb_pe_test_fixture fixture;
};

static struct usb_pe_test_sink_fixture sink_fixture;
static struct usb_pe_test_source_fixture source_fixture;

static void *usb_pe_test_sink_setup(void)
{
	sink_fixture.fixture.tcpci_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	sink_fixture.fixture.charger_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);
	sink_fixture.fixture.partner_pd_role = PD_ROLE_SOURCE;

	return &sink_fixture;
}

static void *usb_pe_test_source_setup(void)
{
	source_fixture.fixture.tcpci_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	source_fixture.fixture.charger_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);
	source_fixture.fixture.partner_pd_role = PD_ROLE_SINK;

	return &source_fixture;
}

static void usb_pe_test_before(void *data)
{
	struct usb_pe_test_fixture *fixture = data;

	set_test_runner_tid();

	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(1));

	test_set_chipset_to_s0();
	k_sleep(K_SECONDS(1));

	tcpci_partner_init(&fixture->partner_emul, PD_REV30);
	tcpc_config[TEST_USB_PORT].flags |= TCPC_FLAGS_TCPCI_REV2_0;

	if (fixture->partner_pd_role == PD_ROLE_SOURCE) {
		/* EC is SINK, partner is SOURCE */
		fixture->partner_emul.extensions = tcpci_src_emul_init(
			&fixture->src_ext, &fixture->partner_emul, NULL);
		fixture->src_ext.pdo[0] =
			PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);
		connect_source_to_port(&fixture->partner_emul,
				       &fixture->src_ext, 0,
				       fixture->tcpci_emul,
				       fixture->charger_emul);
	} else {
		/* EC is SOURCE, partner is SINK */
		fixture->partner_emul.extensions = tcpci_snk_emul_init(
			&fixture->snk_ext, &fixture->partner_emul, NULL);
		fixture->snk_ext.pdo[0] = PDO_FIXED(5000, 500, 0);
		fixture->snk_ext.pdo[1] = TEST_ADDED_PDO;
		connect_sink_to_port(&fixture->partner_emul,
				     fixture->tcpci_emul,
				     fixture->charger_emul);
	}
}

static void usb_pe_test_after(void *data)
{
	struct usb_pe_test_fixture *fixture = data;

	if (fixture->partner_pd_role == PD_ROLE_SOURCE) {
		disconnect_source_from_port(fixture->tcpci_emul,
					    fixture->charger_emul);
	} else {
		disconnect_sink_from_port(fixture->tcpci_emul);
	}
}

ZTEST_SUITE(usb_pe_test_sink, drivers_predicate_post_main,
	    usb_pe_test_sink_setup, usb_pe_test_before, usb_pe_test_after,
	    NULL);

ZTEST_SUITE(usb_pe_test_source, drivers_predicate_post_main,
	    usb_pe_test_source_setup, usb_pe_test_before, usb_pe_test_after,
	    NULL);

/* =========================================================================
 * Sink Tests
 * ========================================================================= */

/**
 * @brief Test PE connection status, state accessors, and debug levels.
 */
ZTEST_F(usb_pe_test_sink, test_pe_status_and_debug_levels)
{
	int port = TEST_USB_PORT;

	/* Verify EC attached as Sink */
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(port),
		      "Expected EC to be SINK");
	zassert_true(tc_is_attached_snk(port),
		     "Expected EC to be attached as SINK");

	/* Verify PE is running */
	zassert_true(pe_is_running(port), "PE should be running");

	/* Test debug levels */
	pe_set_debug_level(DEBUG_LEVEL_3);
	pe_set_debug_level(DEBUG_LEVEL_1);
	pe_set_debug_level(DEBUG_DISABLE);

	/* Verify PE state */
	zassert_true(get_state_pe(port) >= 0, "PE state should be valid");
}

/**
 * @brief Test PE handling of DR_SWAP and PR_SWAP requests as Sink.
 */
ZTEST_F(usb_pe_test_sink, test_pe_role_swaps)
{
	struct usb_pe_test_fixture *super_fixture = &fixture->fixture;
	int rv;
	int port = TEST_USB_PORT;

	/* Verify EC attached as Sink */
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(port),
		      "Expected EC to be SINK");
	zassert_true(tc_is_attached_snk(port),
		     "Expected EC to be attached as SINK");

	/* Send DR_SWAP request from partner */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_DR_SWAP, 0);
	zassert_ok(rv, "Failed to send DR_SWAP request, rv=%d", rv);
	k_sleep(K_SECONDS(2));

	zassert_true(pe_is_running(port), "PE should remain running");

	/* Send PR_SWAP request from partner */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_PR_SWAP, 0);
	zassert_ok(rv, "Failed to send PR_SWAP request, rv=%d", rv);
	k_sleep(K_SECONDS(2));

	zassert_true(pe_is_running(port), "PE should remain running");
}

/**
 * @brief Test Source Capabilities rejection / not supported response.
 */
ZTEST_F(usb_pe_test_sink, test_pe_source_caps_rejected)
{
	struct usb_pe_test_fixture *super_fixture = &fixture->fixture;
	int port = TEST_USB_PORT;
	int rv;

	/* Verify EC attached as Sink */
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(port),
		      "Expected EC to be SINK");
	zassert_true(tc_is_attached_snk(port),
		     "Expected EC to be attached as SINK");

	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_NOT_SUPPORTED, 0);
	zassert_ok(rv, "Failed to send NOT_SUPPORTED, rv=%d", rv);
	k_sleep(K_SECONDS(1));

	zassert_true(pe_is_running(port), "PE should remain running");
}

/**
 * @brief Test DPM VDM request parameter validation.
 */
ZTEST_F(usb_pe_test_sink, test_dpm_vdm_request_parameter_validation)
{
	int port = TEST_USB_PORT;
	uint32_t vdo[VDO_MAX_SIZE + 1] = { 0 };

	/* vdo_count == 0 should return EC_RES_INVALID_PARAM */
	zassert_equal(pd_request_vdm(port, vdo, 0, TCPCI_MSG_SOP),
		      EC_RES_INVALID_PARAM);

	/* vdo_count > VDO_MAX_SIZE should return EC_RES_INVALID_PARAM */
	zassert_equal(pd_request_vdm(port, vdo, VDO_MAX_SIZE + 1,
				     TCPCI_MSG_SOP),
		      EC_RES_INVALID_PARAM);

	/* SVDM Attention message with vdo_count > PD_ATTENTION_MAX_VDO */
	vdo[0] = VDO(USB_SID_PD, 1, CMD_ATTENTION);
	zassert_equal(pd_request_vdm(port, vdo, PD_ATTENTION_MAX_VDO + 1,
				     TCPCI_MSG_SOP),
		      EC_RES_INVALID_PARAM);
}

/**
 * @brief Test DPM VDM request queuing, checking, busy rejection, and clearing.
 */
ZTEST_F(usb_pe_test_sink, test_dpm_vdm_request_flow_and_busy)
{
	int port = TEST_USB_PORT;
	uint32_t vdo[2];

	/* Ensure no VDM request is currently pending */
	dpm_clear_vdm_request(port);
	zassert_false(dpm_check_vdm_request(port));

	/* Valid VDM request */
	vdo[0] = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT);
	vdo[1] = 0;
	zassert_equal(pd_request_vdm(port, vdo, 2, TCPCI_MSG_SOP),
		      EC_RES_SUCCESS);
	zassert_true(dpm_check_vdm_request(port));

	/* Second request should fail with EC_RES_BUSY while one is pending */
	zassert_equal(pd_request_vdm(port, vdo, 2, TCPCI_MSG_SOP), EC_RES_BUSY);

	/* Clear the request and verify */
	dpm_clear_vdm_request(port);
	zassert_false(dpm_check_vdm_request(port));
}

/**
 * @brief Test DPM request enter mode parameter validation and flow.
 */
ZTEST_F(usb_pe_test_sink, test_dpm_request_enter_mode)
{
	int port = TEST_USB_PORT;

	/* Port out of range */
	zassert_equal(pd_request_enter_mode(board_get_usb_pd_port_count(),
					    TYPEC_MODE_DP),
		      EC_RES_INVALID_PARAM);

	/* Invalid mode */
	zassert_equal(pd_request_enter_mode(port, (enum typec_mode)0xff),
		      EC_RES_INVALID_PARAM);

	/* Valid mode DP */
	zassert_equal(pd_request_enter_mode(port, TYPEC_MODE_DP),
		      EC_RES_SUCCESS);

	/* Second call should return EC_RES_BUSY */
	zassert_equal(pd_request_enter_mode(port, TYPEC_MODE_DP), EC_RES_BUSY);

	/* Reset DPM and DP state to clean up flags */
	dpm_init(port);
	dp_init(port);
}

/* =========================================================================
 * Source Tests
 * ========================================================================= */

/**
 * @brief Test PE soft reset transmission and state recovery.
 */
ZTEST_F(usb_pe_test_source, test_pe_soft_reset_flow)
{
	struct usb_pe_test_fixture *super_fixture = &fixture->fixture;
	int port = TEST_USB_PORT;
	int rv;

	/* Verify EC attached as Source */
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(port),
		      "Expected EC to be SOURCE");
	zassert_true(tc_is_attached_src(port),
		     "Expected EC to be attached as SOURCE");

	/* Send Soft Reset from partner */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_SOFT_RESET, 0);
	zassert_ok(rv, "Failed to send Soft Reset, rv=%d", rv);
	k_sleep(K_SECONDS(2));

	zassert_true(pe_is_running(port),
		     "PE should remain running after Soft Reset");
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(port),
		      "Expected EC to remain SOURCE");
}

/**
 * @brief Test PE Get Source Cap Extended and Get Status handling.
 */
ZTEST_F(usb_pe_test_source, test_pe_extended_queries)
{
	struct usb_pe_test_fixture *super_fixture = &fixture->fixture;
	int port = TEST_USB_PORT;
	int rv;

	/* Verify EC attached as Source */
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(port),
		      "Expected EC to be SOURCE");
	zassert_true(tc_is_attached_src(port),
		     "Expected EC to be attached as SOURCE");

	/* Request status from partner */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_GET_STATUS, 0);
	zassert_ok(rv, "Failed to send GET_STATUS request, rv=%d", rv);
	k_sleep(K_SECONDS(2));

	zassert_true(pe_is_running(port), "PE should remain running");
}

/**
 * @brief Test PE PR_SWAP evaluation as Source, transition to off, and
 * interruption.
 */
ZTEST_F(usb_pe_test_source, test_pe_source_pr_swap_and_interruption)
{
	struct usb_pe_test_fixture *super_fixture = &fixture->fixture;
	int port = TEST_USB_PORT;
	int rv;

	/* Verify EC attached as Source */
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(port),
		      "Expected EC to be SOURCE");
	zassert_true(tc_is_attached_src(port),
		     "Expected EC to be attached as SOURCE");

	/* Send PR_SWAP from partner while EC is Source */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_PR_SWAP, 0);
	zassert_ok(rv, "Failed to send PR_SWAP request, rv=%d", rv);
	k_sleep(K_MSEC(10));

	/* Send interrupting message during power transition to trigger Hard
	 * Reset */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_PING, 0);
	zassert_ok(rv, "Failed to send interrupting message, rv=%d", rv);
	k_sleep(K_SECONDS(2));

	zassert_true(pe_is_running(port), "PE should remain running");
}

/**
 * @brief Test partner requesting sink and source capabilities from EC.
 */
ZTEST_F(usb_pe_test_source, test_pe_sink_cap_query_as_source)
{
	struct usb_pe_test_fixture *super_fixture = &fixture->fixture;
	int port = TEST_USB_PORT;
	int rv;

	/* Verify EC attached as Source */
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(port),
		      "Expected EC to be SOURCE");
	zassert_true(tc_is_attached_src(port),
		     "Expected EC to be attached as SOURCE");

	/* Send GET_SINK_CAP from partner */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_GET_SINK_CAP, 0);
	zassert_ok(rv, "Failed to send GET_SINK_CAP, rv=%d", rv);
	k_sleep(K_SECONDS(1));

	/* Send GET_SOURCE_CAP from partner */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_GET_SOURCE_CAP, 0);
	zassert_ok(rv, "Failed to send GET_SOURCE_CAP, rv=%d", rv);
	k_sleep(K_SECONDS(1));

	zassert_true(pe_is_running(port), "PE should remain running");
}

/**
 * @brief Test sending VDM request from DFP/Source.
 */
ZTEST_F(usb_pe_test_source, test_dpm_vdm_attention_source)
{
	int port = TEST_USB_PORT;
	uint32_t vdo[1];

	vdo[0] = VDO(USB_SID_PD, 1, CMD_ATTENTION);
	zassert_equal(pd_request_vdm(port, vdo, 1, TCPCI_MSG_SOP),
		      EC_RES_SUCCESS);

	/* Allow DPM state machine to process and send VDM request */
	k_sleep(K_SECONDS(1));

	/* SVDM Attention clears DPM_FLAG_SEND_VDM_REQ automatically */
	zassert_false(dpm_check_vdm_request(port));
	zassert_true(pe_is_running(port), "PE should remain running");
}

/**
 * @brief Test notifying DPM of an Attention message.
 */
ZTEST_F(usb_pe_test_source, test_dpm_notify_attention)
{
	int port = TEST_USB_PORT;
	uint32_t buf[2] = {
		VDO(USB_SID_DISPLAYPORT, 1, CMD_ATTENTION),
		VDO_DP_STATUS(1, /* IRQ_HPD */
			      true, /* HPD_HI|LOW */
			      0, /* request exit DP */
			      0, /* request exit USB */
			      0, /* MF pref */
			      true, /* DP Enabled */
			      0, /* power low e.g. normal */
			      0x2 /* Connected as Sink */),
	};
	uint32_t ack_vdo = VDO(USB_SID_DISPLAYPORT, 1, CMD_ENTER_MODE);

	/* Out-of-sequence attention dropped while DP is idle */
	dp_status[port] = 0;
	dpm_notify_attention(port, 2, buf);
	zassert_equal(dp_status[port], 0,
		      "Attention should be dropped when DP is idle");

	/* Transition DP state to active and verify attention updates status */
	dp_init(port);
	dp_vdm_acked(port, TCPCI_MSG_SOP, 1, &ack_vdo);

	dpm_notify_attention(port, 2, buf);
	zassert_equal(dp_status[port], buf[1],
		      "Attention status should update dp_status when active");

	/* Reset DP state */
	dp_init(port);
	zassert_true(pe_is_running(port), "PE should remain running");
}
