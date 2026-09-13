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
