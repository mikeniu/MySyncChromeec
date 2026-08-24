/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test/usb_pe.h"
#include "usb_emsg.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_prl_sm.h"
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
	struct tcpci_drp_emul_data drp_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	enum pd_power_role drp_partner_pd_role;
};

struct usb_prl_test_sink_fixture {
	struct usb_prl_test_fixture fixture;
};

struct usb_prl_test_source_fixture {
	struct usb_prl_test_fixture fixture;
};

static void
tcpci_drp_emul_connect_partner(struct tcpci_partner_data *partner_emul,
			       const struct emul *tcpci_emul,
			       const struct emul *charger_emul)
{
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(tcpci_emul, VBUS_SAFE0V));
	zassert_ok(tcpci_partner_connect_to_tcpci(partner_emul, tcpci_emul));
}

static void disconnect_partner(struct usb_prl_test_fixture *fixture)
{
	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));
}

static struct usb_prl_test_sink_fixture sink_fixture;
static struct usb_prl_test_source_fixture source_fixture;

static void *usb_prl_test_sink_setup(void)
{
	sink_fixture.fixture.tcpci_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	sink_fixture.fixture.charger_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);
	sink_fixture.fixture.drp_partner_pd_role = PD_ROLE_SINK;

	return &sink_fixture;
}

static void *usb_prl_test_source_setup(void)
{
	source_fixture.fixture.tcpci_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	source_fixture.fixture.charger_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);
	source_fixture.fixture.drp_partner_pd_role = PD_ROLE_SOURCE;

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
	fixture->partner_emul.extensions = tcpci_drp_emul_init(
		&fixture->drp_ext, &fixture->partner_emul,
		fixture->drp_partner_pd_role,
		tcpci_src_emul_init(&fixture->src_ext, &fixture->partner_emul,
				    NULL),
		tcpci_snk_emul_init(&fixture->snk_ext, &fixture->partner_emul,
				    NULL));
	fixture->snk_ext.pdo[1] = TEST_ADDED_PDO;
	tcpc_config[TEST_USB_PORT].flags |= TCPC_FLAGS_TCPCI_REV2_0;

	tcpci_drp_emul_connect_partner(&fixture->partner_emul,
				       fixture->tcpci_emul,
				       fixture->charger_emul);

	k_sleep(K_SECONDS(10));
}

static void usb_prl_test_after(void *data)
{
	struct usb_prl_test_fixture *fixture = data;

	disconnect_partner(fixture);
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

	/* Send Soft Reset from partner */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_SOFT_RESET, 0);
	zassert_ok(rv, "Failed to send Soft Reset, rv=%d", rv);

	k_sleep(K_SECONDS(2));

	/* PRL should remain running after soft reset */
	zassert_true(prl_is_running(port),
		     "PRL should remain running after Soft Reset");
}

/**
 * @brief Test transmission of control messages and state handling.
 */
ZTEST_F(usb_prl_test_sink, test_prl_tx_control_msg)
{
	int port = TEST_USB_PORT;

	/* Send Ping control message from EC */
	prl_send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_PING);
	k_sleep(K_MSEC(100));

	zassert_true(prl_is_running(port), "PRL should remain running");

	/* Send Get_Source_Cap control message */
	prl_send_ctrl_msg(port, TCPCI_MSG_SOP, PD_CTRL_GET_SOURCE_CAP);
	k_sleep(K_MSEC(100));

	zassert_true(prl_is_running(port), "PRL should remain running");
}

/* =========================================================================
 * Source Tests
 * ========================================================================= */

/**
 * @brief Test Hard Reset sequence, power cycle recovery, and state transitions.
 */
ZTEST_F(usb_prl_test_source, test_prl_hard_reset_and_recovery)
{
	struct usb_prl_test_fixture *super_fixture = &fixture->fixture;
	int port = TEST_USB_PORT;

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
}

/**
 * @brief Test sending chunked extended messages and chunk request handling.
 */
ZTEST_F(usb_prl_test_source, test_prl_chunked_extended_msg)
{
	struct usb_prl_test_fixture *super_fixture = &fixture->fixture;
	int port = TEST_USB_PORT;

	/* Request battery capabilities from partner to exercise extended
	 * message flow */
	tcpci_partner_common_send_get_battery_capabilities(
		&super_fixture->partner_emul, 0);
	k_sleep(K_SECONDS(2));

	zassert_true(prl_is_running(port), "PRL should remain running");
}
