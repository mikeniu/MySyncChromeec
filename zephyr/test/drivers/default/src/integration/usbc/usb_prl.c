/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test/usb_pe.h"
#include "test/usb_prl.h"
#include "usb_emsg.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"
#include "util.h"

#include <stdint.h>

#include <zephyr/ztest.h>

#define TEST_USB_PORT 0
BUILD_ASSERT(TEST_USB_PORT == USBC_PORT_C0);

#define TEST_ADDED_PDO PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED)

struct usb_prl_test_fixture {
	struct tcpci_partner_data partner_emul;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_src_emul_data src_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	enum pd_power_role partner_pd_role;
};

struct usb_prl_test_sink_fixture {
	struct usb_prl_test_fixture fixture;
};

struct usb_prl_test_source_fixture {
	struct usb_prl_test_fixture fixture;
};

static struct usb_prl_test_sink_fixture sink_fixture;
static struct usb_prl_test_source_fixture source_fixture;

static void *usb_prl_test_sink_setup(void)
{
	sink_fixture.fixture.tcpci_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	sink_fixture.fixture.charger_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);
	sink_fixture.fixture.partner_pd_role = PD_ROLE_SOURCE;

	return &sink_fixture;
}

static void *usb_prl_test_source_setup(void)
{
	source_fixture.fixture.tcpci_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	source_fixture.fixture.charger_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);
	source_fixture.fixture.partner_pd_role = PD_ROLE_SINK;

	return &source_fixture;
}

static void usb_prl_test_before(void *data)
{
	struct usb_prl_test_fixture *fixture = data;

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

static void usb_prl_test_after(void *data)
{
	struct usb_prl_test_fixture *fixture = data;

	if (fixture->partner_pd_role == PD_ROLE_SOURCE) {
		disconnect_source_from_port(fixture->tcpci_emul,
					    fixture->charger_emul);
	} else {
		disconnect_sink_from_port(fixture->tcpci_emul);
	}
}

ZTEST_SUITE(usb_prl_test_sink, drivers_predicate_post_main,
	    usb_prl_test_sink_setup, usb_prl_test_before, usb_prl_test_after,
	    NULL);

ZTEST_SUITE(usb_prl_test_source, drivers_predicate_post_main,
	    usb_prl_test_source_setup, usb_prl_test_before, usb_prl_test_after,
	    NULL);

static void send_raw_rx_msg(const struct emul *tcpci_emul,
			    enum tcpci_msg_type sop_type, const uint8_t *buf,
			    int cnt)
{
	struct tcpci_partner_msg *msg = calloc(1, sizeof(*msg));

	zassert_not_null(msg, "Failed to allocate partner msg");
	msg->msg.buf = calloc(1, cnt);
	zassert_not_null(msg->msg.buf, "Failed to allocate msg buf");
	memcpy(msg->msg.buf, buf, cnt);
	msg->msg.cnt = cnt;
	msg->msg.sop_type = sop_type;
	zassert_ok(tcpci_emul_add_rx_msg(tcpci_emul, &msg->msg, true));
}

/* =========================================================================
 * Sink Tests
 * ========================================================================= */

/**
 * @brief Test PRL state queries, revision setting/getting, and debug levels.
 */
ZTEST_F(usb_prl_test_sink, test_prl_status_and_revision_apis)
{
	int port = TEST_USB_PORT;

	/* Verify EC attached as Sink */
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(port),
		      "Expected EC to be SINK");
	zassert_true(tc_is_attached_snk(port),
		     "Expected EC to be attached as SINK");

	/* PRL should be running while attached */
	zassert_true(prl_is_running(port), "PRL should be running");

	/* Test debug levels */
	prl_set_debug_level(DEBUG_LEVEL_3);
	prl_set_debug_level(DEBUG_LEVEL_1);
	prl_set_debug_level(DEBUG_DISABLE);

	/* Test getting and setting revision */
	prl_set_rev(port, TCPCI_MSG_SOP, PD_REV20);
	zassert_equal(PD_REV20, prl_get_rev(port, TCPCI_MSG_SOP),
		      "Expected PD_REV20");

	prl_set_rev(port, TCPCI_MSG_SOP_PRIME, PD_REV30);
	zassert_equal(PD_REV30, prl_get_rev(port, TCPCI_MSG_SOP_PRIME),
		      "Expected PD_REV30 for SOP'");

	prl_set_rev(port, TCPCI_MSG_SOP_PRIME_PRIME, PD_REV30);
	zassert_equal(PD_REV30, prl_get_rev(port, TCPCI_MSG_SOP_PRIME_PRIME),
		      "Expected PD_REV30 for SOP''");

	prl_set_rev(port, TCPCI_MSG_SOP, PD_REV30);
	zassert_equal(PD_REV30, prl_get_rev(port, TCPCI_MSG_SOP),
		      "Expected PD_REV30");

	prl_set_default_pd_revision(port);
	zassert_equal(PD_REV30, prl_get_rev(port, TCPCI_MSG_SOP),
		      "Expected default PD_REV30");

	/* Verify busy state when idle */
	zassert_false(prl_is_busy(port), "PRL should not be busy when idle");

	/* Test soft reset call */
	prl_reset_soft(port);
	k_sleep(K_MSEC(50));
	zassert_true(prl_is_running(port), "PRL should be running");
}

/**
 * @brief Test Soft Reset exchange and protocol state recovery.
 */
ZTEST_F(usb_prl_test_sink, test_prl_soft_reset)
{
	struct usb_prl_test_fixture *super_fixture = &fixture->fixture;
	int rv;
	int port = TEST_USB_PORT;

	/* Verify EC attached as Sink */
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(port),
		      "Expected EC to be SINK");
	zassert_true(tc_is_attached_snk(port),
		     "Expected EC to be attached as SINK");

	/* Send Soft Reset from partner */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_SOFT_RESET, 0);
	zassert_ok(rv, "Failed to send Soft Reset, rv=%d", rv);

	k_sleep(K_SECONDS(2));

	/* PRL should remain running after soft reset */
	zassert_true(prl_is_running(port),
		     "PRL should remain running after Soft Reset");
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(port),
		      "Expected EC to remain SINK");
}

/**
 * @brief Test transmission of control messages and state handling.
 */
ZTEST_F(usb_prl_test_sink, test_prl_tx_control_msg)
{
	int port = TEST_USB_PORT;

	/* Verify EC attached as Sink */
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(port),
		      "Expected EC to be SINK");
	zassert_true(tc_is_attached_snk(port),
		     "Expected EC to be attached as SINK");

	/* Send Ping control message from EC */
	prl_send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_PING);
	k_sleep(K_MSEC(100));

	zassert_true(prl_is_running(port), "PRL should remain running");

	/* Send Get_Source_Cap control message */
	prl_send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_GET_SOURCE_CAP);
	k_sleep(K_MSEC(100));

	zassert_true(prl_is_running(port), "PRL should remain running");
}

/**
 * @brief Test Hard Reset sequence, power cycle recovery, and state transitions
 * in Sink mode.
 */
ZTEST_F(usb_prl_test_sink, test_prl_hard_reset_and_recovery)
{
	struct usb_prl_test_fixture *super_fixture = &fixture->fixture;
	int port = TEST_USB_PORT;

	/* Verify EC attached as Sink */
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(port),
		      "Expected EC to be SINK");
	zassert_true(tc_is_attached_snk(port),
		     "Expected EC to be attached as SINK");

	/* Send Hard Reset from partner */
	tcpci_partner_common_send_hard_reset(&super_fixture->partner_emul);
	k_sleep(K_MSEC(30));
	isl923x_emul_set_adc_vbus(super_fixture->charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(super_fixture->tcpci_emul,
					     VBUS_SAFE0V));

	k_sleep(K_MSEC(660));
	isl923x_emul_set_adc_vbus(super_fixture->charger_emul, 5000);
	zassert_ok(tcpci_emul_set_vbus_level(super_fixture->tcpci_emul,
					     VBUS_PRESENT));

	k_sleep(K_SECONDS(3));

	/* Verify system recovered and PRL is active */
	zassert_true(prl_is_running(port),
		     "PRL should be running after Hard Reset recovery");
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(port),
		      "Expected EC to remain SINK after recovery");
}

/**
 * @brief Test PRL state queries, debug prints, and Hard Reset complete exit.
 */
ZTEST_F(usb_prl_test_sink, test_prl_state_queries_and_hard_reset_exit)
{
	int port = TEST_USB_PORT;

	/* Enable maximum debug level to cover all state machine log prints */
	prl_set_debug_level(DEBUG_LEVEL_3);

	/* Check initial states */
	zassert_equal(PRL_TX_WAIT_FOR_MESSAGE_REQUEST, prl_tx_get_state(port),
		      "Expected PRL_TX_WAIT_FOR_MESSAGE_REQUEST");
	zassert_equal(PRL_HR_WAIT_FOR_REQUEST, prl_hr_get_state(port),
		      "Expected PRL_HR_WAIT_FOR_REQUEST");
	zassert_equal(RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER,
		      rch_get_state(port),
		      "Expected RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER");
	zassert_equal(TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE, tch_get_state(port),
		      "Expected TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE");

	/* Test hard reset execution and PE complete signal */
	prl_execute_hard_reset(port);
	k_sleep(K_MSEC(50));

	/* Verify Hard Reset state machine reached wait for PE complete */
	zassert_equal(PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE,
		      prl_hr_get_state(port),
		      "Expected PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE");

	/* Signal hard reset complete to transition and hit exit handler */
	prl_hard_reset_complete(port);
	k_sleep(K_MSEC(50));

	zassert_equal(PRL_HR_WAIT_FOR_REQUEST, prl_hr_get_state(port),
		      "Expected PRL_HR_WAIT_FOR_REQUEST after reset complete");

	/* Calling hard reset on unattached port 1 should early return */
	if (CONFIG_USB_PD_PORT_MAX_COUNT > 1) {
		prl_execute_hard_reset(1);
	}

	/* Test data role checking toggle */
	prl_set_data_role_check(port, true);
	prl_set_data_role_check(port, false);
	prl_set_data_role_check(port, true);

	prl_set_debug_level(DEBUG_DISABLE);
}

/**
 * @brief Test PRL message reception filtering, duplicate discard, and count
 * clamping.
 */
ZTEST_F(usb_prl_test_sink, test_prl_rx_filtering_and_edge_cases)
{
	struct usb_prl_test_fixture *super_fixture = &fixture->fixture;
	uint8_t raw_buf[36];
	uint16_t hdr;
	int port = TEST_USB_PORT;
	int rv;

	prl_set_debug_level(DEBUG_LEVEL_3);

	/* 1. Test duplicate message ID filtering (line 2265) */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_PING, 0);
	zassert_ok(rv, "Failed to send Ping, rv=%d", rv);
	k_sleep(K_MSEC(50));

	/* Send a raw message with identical message ID */
	hdr = PD_HEADER(PD_CTRL_PING, super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			(super_fixture->partner_emul.sop_msg_id - 1) & 0x7, 0,
			PD_REV30, 0);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf, 2);
	k_sleep(K_MSEC(50));

	/* 2. Test dropping non-SOP message with prole == PD_PLUG_FROM_DFP_UFP
	 * (lines 2213-2214) */
	tcpm_sop_prime_enable(port, true);
	hdr = PD_HEADER(PD_CTRL_PING, PD_PLUG_FROM_DFP_UFP, 0,
			super_fixture->partner_emul.sop_prime_msg_id & 0x7, 0,
			PD_REV30, 0);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP_PRIME, raw_buf,
			2);
	k_sleep(K_MSEC(50));
	tcpm_sop_prime_enable(port, false);

	/* 3. Test message count clamping when cnt > CHK_BUF_SIZE (line 2194) */
	memset(raw_buf, 0, sizeof(raw_buf));
	hdr = PD_HEADER(PD_DATA_VENDOR_DEF,
			super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			super_fixture->partner_emul.sop_msg_id & 0x7, 8,
			PD_REV30, 0);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf,
			2 + 8 * 4);
	k_sleep(K_MSEC(50));

	prl_set_debug_level(DEBUG_DISABLE);
}

/**
 * @brief Test Chunked Rx multi-chunk flow, chunk requesting, waiting, and
 * error reporting.
 */
ZTEST_F(usb_prl_test_sink, test_prl_chunked_rx_multi_chunk_flow)
{
	struct usb_prl_test_fixture *super_fixture = &fixture->fixture;
	uint8_t raw_buf[32];
	uint16_t hdr;
	uint16_t exthdr;
	int port = TEST_USB_PORT;

	prl_set_debug_level(DEBUG_LEVEL_3);

	/* 1. Multi-chunk incoming message leading to chunk response timer
	 * timeout:
	 * Send Chunk 0 with total data size = 30 bytes (> 26 bytes chunk max).
	 * Header: cnt = 7 (28 bytes: 2 bytes ext header + 26 bytes payload).
	 */
	memset(raw_buf, 0xa5, sizeof(raw_buf));
	hdr = PD_HEADER(PD_EXT_SECURITY_REQUEST,
			super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			super_fixture->partner_emul.sop_msg_id & 0x7, 7,
			PD_REV30, 1);
	exthdr = PD_EXT_HEADER(0, 0, 30);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	raw_buf[2] = exthdr & 0xff;
	raw_buf[3] = (exthdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf,
			2 + 28);

	/* Allow EC to process chunk 0, transition to RCH_REQUESTING_CHUNK, send
	 * Chunk Request, enter RCH_WAITING_CHUNK, and let
	 * ChunkSenderResponseTimer expire.
	 */
	k_sleep(K_MSEC(80));

	zassert_equal(RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER,
		      rch_get_state(port),
		      "Expected RCH to return to wait state after timeout");

	/* 2. Multi-chunk incoming message with successful chunk completion:
	 * Send Chunk 0 (26 bytes of 30), then send Chunk 1 (remaining 4 bytes).
	 */
	memset(raw_buf, 0x5a, sizeof(raw_buf));
	super_fixture->partner_emul.sop_msg_id++;
	hdr = PD_HEADER(PD_EXT_SECURITY_REQUEST,
			super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			super_fixture->partner_emul.sop_msg_id & 0x7, 7,
			PD_REV30, 1);
	exthdr = PD_EXT_HEADER(0, 0, 30);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	raw_buf[2] = exthdr & 0xff;
	raw_buf[3] = (exthdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf,
			2 + 28);
	k_sleep(K_MSEC(10));

	/* Send Chunk 1 (chunk_num = 1, cnt = 2 objects = 8 bytes) */
	super_fixture->partner_emul.sop_msg_id++;
	hdr = PD_HEADER(PD_EXT_SECURITY_REQUEST,
			super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			super_fixture->partner_emul.sop_msg_id & 0x7, 2,
			PD_REV30, 1);
	exthdr = PD_EXT_HEADER(1, 0, 30);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	raw_buf[2] = exthdr & 0xff;
	raw_buf[3] = (exthdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf,
			2 + 8);
	k_sleep(K_MSEC(50));

	/* 3. Unexpected Chunk Number while processing extended message */
	super_fixture->partner_emul.sop_msg_id++;
	hdr = PD_HEADER(PD_EXT_SECURITY_REQUEST,
			super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			super_fixture->partner_emul.sop_msg_id & 0x7, 7,
			PD_REV30, 1);
	exthdr = PD_EXT_HEADER(0, 0, 30);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	raw_buf[2] = exthdr & 0xff;
	raw_buf[3] = (exthdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf,
			2 + 28);
	k_sleep(K_MSEC(10));

	/* Send Chunk 5 instead of expected Chunk 1 */
	super_fixture->partner_emul.sop_msg_id++;
	hdr = PD_HEADER(PD_EXT_SECURITY_REQUEST,
			super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			super_fixture->partner_emul.sop_msg_id & 0x7, 2,
			PD_REV30, 1);
	exthdr = PD_EXT_HEADER(5, 0, 30);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	raw_buf[2] = exthdr & 0xff;
	raw_buf[3] = (exthdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf,
			2 + 8);
	k_sleep(K_MSEC(50));

	/* 4. Receiving unexpected Chunk Request in RCH_WAITING_CHUNK */
	super_fixture->partner_emul.sop_msg_id++;
	hdr = PD_HEADER(PD_EXT_SECURITY_REQUEST,
			super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			super_fixture->partner_emul.sop_msg_id & 0x7, 7,
			PD_REV30, 1);
	exthdr = PD_EXT_HEADER(0, 0, 30);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	raw_buf[2] = exthdr & 0xff;
	raw_buf[3] = (exthdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf,
			2 + 28);
	k_sleep(K_MSEC(10));

	/* Send Chunk Request while EC is in RCH_WAITING_CHUNK */
	super_fixture->partner_emul.sop_msg_id++;
	hdr = PD_HEADER(PD_EXT_SECURITY_REQUEST,
			super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			super_fixture->partner_emul.sop_msg_id & 0x7, 1,
			PD_REV30, 1);
	exthdr = PD_EXT_HEADER(0, 1 /* req_chunk */, 0);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	raw_buf[2] = exthdr & 0xff;
	raw_buf[3] = (exthdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf,
			2 + 4);
	k_sleep(K_MSEC(50));

	/* 5. Extended message received while revision is lower than PD3.0 */
	prl_set_rev(port, TCPCI_MSG_SOP, PD_REV20);
	super_fixture->partner_emul.sop_msg_id++;
	hdr = PD_HEADER(PD_EXT_SECURITY_REQUEST,
			super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			super_fixture->partner_emul.sop_msg_id & 0x7, 1,
			PD_REV20, 1);
	exthdr = PD_EXT_HEADER(0, 0, 4);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	raw_buf[2] = exthdr & 0xff;
	raw_buf[3] = (exthdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf,
			2 + 4);
	k_sleep(K_MSEC(50));

	/* Restore revision */
	prl_set_rev(port, TCPCI_MSG_SOP, PD_REV30);
	prl_set_debug_level(DEBUG_DISABLE);
}

/* =========================================================================
 * Source Tests
 * ========================================================================= */

/**
 * @brief Test sending chunked extended messages and chunk request handling.
 */
ZTEST_F(usb_prl_test_source, test_prl_chunked_extended_msg)
{
	struct usb_prl_test_fixture *super_fixture = &fixture->fixture;
	int port = TEST_USB_PORT;

	/* Verify EC attached as Source */
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(port),
		      "Expected EC to be SOURCE");
	zassert_true(tc_is_attached_src(port),
		     "Expected EC to be attached as SOURCE");

	/* Request battery capabilities from partner to exercise extended
	 * message flow */
	tcpci_partner_common_send_get_battery_capabilities(
		&super_fixture->partner_emul, 0);
	k_sleep(K_SECONDS(2));

	zassert_true(prl_is_running(port), "PRL should remain running");
}

/**
 * @brief Test Soft Reset exchange and protocol state recovery in Source mode.
 */
ZTEST_F(usb_prl_test_source, test_prl_source_soft_reset)
{
	struct usb_prl_test_fixture *super_fixture = &fixture->fixture;
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

	/* PRL should remain running after soft reset */
	zassert_true(prl_is_running(port),
		     "PRL should remain running after Soft Reset");
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(port),
		      "Expected EC to remain SOURCE");
}

/**
 * @brief Test transmission of control messages in Source mode.
 */
ZTEST_F(usb_prl_test_source, test_prl_source_tx_control_msg)
{
	int port = TEST_USB_PORT;

	/* Verify EC attached as Source */
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(port),
		      "Expected EC to be SOURCE");
	zassert_true(tc_is_attached_src(port),
		     "Expected EC to be attached as SOURCE");

	/* Send Ping control message from EC */
	prl_send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_PING);
	k_sleep(K_MSEC(100));

	zassert_true(prl_is_running(port), "PRL should remain running");

	/* Send Get_Sink_Cap control message */
	prl_send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_GET_SINK_CAP);
	k_sleep(K_MSEC(100));

	zassert_true(prl_is_running(port), "PRL should remain running");
}

/**
 * @brief Test Chunked Tx multi-chunk transmission, chunk request response, and
 * error reporting.
 */
ZTEST_F(usb_prl_test_source, test_prl_chunked_tx_multi_chunk_flow)
{
	struct usb_prl_test_fixture *super_fixture = &fixture->fixture;
	uint8_t raw_buf[8];
	uint16_t hdr;
	uint16_t exthdr;
	int port = TEST_USB_PORT;

	/* Verify EC attached as Source */
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(port),
		      "Expected EC to be SOURCE");
	zassert_true(tc_is_attached_src(port),
		     "Expected EC to be attached as SOURCE");

	prl_set_debug_level(DEBUG_LEVEL_3);

	/* 1. Transmit multi-chunk message and let ChunkSenderRequestTimer
	 * expire:
	 * Len = 30 bytes (> 26 bytes). First chunk will be 26 bytes.
	 * EC enters TCH_WAIT_CHUNK_REQUEST.
	 */
	memset(tx_emsg[port].buf, 0x3c, sizeof(tx_emsg[port].buf));
	tx_emsg[port].len = 30;

	prl_send_ext_data_msg(port, TCPCI_MSG_SOP, PD_EXT_SINK_CAP);

	/* Allow transmission of chunk 0, transition to TCH_WAIT_CHUNK_REQUEST,
	 * and wait for ChunkSenderRequestTimer timeout.
	 */
	k_sleep(K_MSEC(60));

	zassert_equal(
		TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE, tch_get_state(port),
		"Expected TCH to return to wait state after message sent");

	/* 2. Transmit multi-chunk message and reply with matching Chunk Request
	 * (chunk 1) */
	tx_emsg[port].len = 30;
	prl_send_ext_data_msg(port, TCPCI_MSG_SOP, PD_EXT_SINK_CAP);
	k_sleep(K_MSEC(10));

	/* Send Chunk Request for chunk 1 */
	super_fixture->partner_emul.sop_msg_id++;
	hdr = PD_HEADER(PD_EXT_SINK_CAP, super_fixture->partner_emul.power_role,
			super_fixture->partner_emul.data_role,
			super_fixture->partner_emul.sop_msg_id & 0x7, 1,
			PD_REV30, 1);
	exthdr = PD_EXT_HEADER(1 /* chunk_num */, 1 /* req_chunk */, 0);
	raw_buf[0] = hdr & 0xff;
	raw_buf[1] = (hdr >> 8) & 0xff;
	raw_buf[2] = exthdr & 0xff;
	raw_buf[3] = (exthdr >> 8) & 0xff;
	send_raw_rx_msg(super_fixture->tcpci_emul, TCPCI_MSG_SOP, raw_buf,
			2 + 4);
	k_sleep(K_MSEC(50));

	zassert_equal(
		TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE, tch_get_state(port),
		"Expected TCH to return to wait state after message sent");

	/* 3. Test data message length clamping when tx_emsg len >
	 * CHK_BUF_SIZE_BYTES (line 835) */
	tx_emsg[port].len = 35;
	prl_send_data_msg(port, TCPCI_MSG_SOP, PD_DATA_VENDOR_DEF);
	k_sleep(K_MSEC(50));

	prl_set_debug_level(DEBUG_DISABLE);
}
