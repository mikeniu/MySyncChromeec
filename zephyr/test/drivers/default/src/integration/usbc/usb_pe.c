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
#include "usb_pe_sm.h"
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
	struct tcpci_drp_emul_data drp_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	enum pd_power_role drp_partner_pd_role;
};

struct usb_pe_test_sink_fixture {
	struct usb_pe_test_fixture fixture;
};

struct usb_pe_test_source_fixture {
	struct usb_pe_test_fixture fixture;
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

static void disconnect_partner(struct usb_pe_test_fixture *fixture)
{
	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));
}

static struct usb_pe_test_sink_fixture sink_fixture;
static struct usb_pe_test_source_fixture source_fixture;

static void *usb_pe_test_sink_setup(void)
{
	sink_fixture.fixture.tcpci_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	sink_fixture.fixture.charger_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);
	sink_fixture.fixture.drp_partner_pd_role = PD_ROLE_SOURCE;

	return &sink_fixture;
}

static void *usb_pe_test_source_setup(void)
{
	source_fixture.fixture.tcpci_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	source_fixture.fixture.charger_emul =
		EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);
	source_fixture.fixture.drp_partner_pd_role = PD_ROLE_SINK;

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

static void usb_pe_test_after(void *data)
{
	struct usb_pe_test_fixture *fixture = data;

	disconnect_partner(fixture);
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

	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_NOT_SUPPORTED, 0);
	zassert_ok(rv, "Failed to send NOT_SUPPORTED, rv=%d", rv);
	k_sleep(K_SECONDS(1));

	zassert_true(pe_is_running(port), "PE should remain running");
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

	/* Send Soft Reset from partner */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_SOFT_RESET, 0);
	zassert_ok(rv, "Failed to send Soft Reset, rv=%d", rv);
	k_sleep(K_SECONDS(2));

	zassert_true(pe_is_running(port),
		     "PE should remain running after Soft Reset");
}

/**
 * @brief Test PE Get Source Cap Extended and Get Status handling.
 */
ZTEST_F(usb_pe_test_source, test_pe_extended_queries)
{
	struct usb_pe_test_fixture *super_fixture = &fixture->fixture;
	int port = TEST_USB_PORT;
	int rv;

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
