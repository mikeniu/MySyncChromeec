/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/tcpci_test_common.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test/usb_pe.h"
#include "usb_pd.h"

#include <stdint.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

enum usb_tc_state {
	NOT_A_REAL_STATE,
};

enum usb_tc_state get_state_tc(const int port);

#define TEST_USB_PORT 0

#define TEST_ADDED_PDO PDO_FIXED(10000, 3000, PDO_FIXED_UNCONSTRAINED)

struct usb_hard_reset_source_fixture {
	struct tcpci_partner_data partner_emul;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_src_emul_data src_ext;
	struct tcpci_drp_emul_data drp_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	enum pd_power_role drp_partner_pd_role;
};

static void *usb_hard_reset_source_setup(void)
{
	static struct usb_hard_reset_source_fixture fixture;

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);
	fixture.drp_partner_pd_role = PD_ROLE_SOURCE;

	return &fixture;
}

static void
tcpci_drp_emul_connect_partner(struct tcpci_partner_data *partner_emul,
			       const struct emul *tcpci_emul,
			       const struct emul *charger_emul)
{
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(tcpci_emul, VBUS_SAFE0V));

	zassert_ok(tcpci_partner_connect_to_tcpci(partner_emul, tcpci_emul),
		   NULL);
	isl923x_emul_set_adc_vbus(charger_emul, 5000);
}

static void usb_hard_reset_before(void *data)
{
	struct usb_hard_reset_source_fixture *fixture = data;

	set_test_runner_tid();

	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(1));

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Initialized DRP */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = tcpci_drp_emul_init(
		&fixture->drp_ext, &fixture->partner_emul,
		fixture->drp_partner_pd_role,
		tcpci_src_emul_init(&fixture->src_ext, &fixture->partner_emul,
				    NULL),
		tcpci_snk_emul_init(&fixture->snk_ext, &fixture->partner_emul,
				    NULL));
	/* Add additional Sink PDO to partner to verify
	 * PE_DR_SNK_Get_Sink_Cap/PE_SRC_Get_Sink_Cap (these are shared PE
	 * states) state was reached
	 */
	fixture->snk_ext.pdo[1] = TEST_ADDED_PDO;

	/* Turn TCPCI rev 2 ON */
	tcpc_config[TEST_USB_PORT].flags |= TCPC_FLAGS_TCPCI_REV2_0;

	tcpci_drp_emul_connect_partner(&fixture->partner_emul,
				       fixture->tcpci_emul,
				       fixture->charger_emul);

	k_sleep(K_SECONDS(10));
}

static void usb_hard_reset_after(void *data)
{
	struct usb_hard_reset_source_fixture *fixture = data;

	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));
}

/** ZTEST_SUITE to setup DRP partner_emul as SOURCE  */
ZTEST_SUITE(usb_hard_reset_source, drivers_predicate_post_main,
	    usb_hard_reset_source_setup, usb_hard_reset_before,
	    usb_hard_reset_after, NULL);

/**
 * @brief TestPurpose: Perform a normal hard reset.
 */
ZTEST_F(usb_hard_reset_source, test_normal)
{
	int state;

	/* Partner sends the Hard Reset. */
	tcpci_partner_common_send_hard_reset(&fixture->partner_emul);
	/* Partner waits ~30 ms - 650 ms (PD_T_SAFE_0V) */
	k_sleep(K_MSEC(30));
	/* Partner drops to vSafe0V */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_SAFE0V));

	/* Partner waits ~660 ms - 1000 ms (PD_T_SRC_RECOVER_MAX)
	 * + 0 ms - 275 ms (PD_T_SRC_TURN_ON)
	 */
	k_sleep(K_MSEC(660));

	/* Partner sets VBUS present */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 5000);
	zassert_ok(
		tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_PRESENT));

	/* Wait for everything to settle, and check state. */
	k_sleep(K_SECONDS(2));
	state = get_state_tc(TEST_USB_PORT);
	zassert_equal(state, /* TC_ATTACHED_SNK */ 7, "Got %d", state);
	state = get_state_pe(TEST_USB_PORT);
	zassert_equal(state, PE_SNK_READY, "Got %d", state);
}

/**
 * @brief TestPurpose: Perform a hard reset where the VBUS doesn't reach
 *        vSave0V on time.
 */
ZTEST_F(usb_hard_reset_source, test_vsafe0v_late)
{
	int state;

	/* Partner sends the Hard Reset. */
	tcpci_partner_common_send_hard_reset(&fixture->partner_emul);
	/* Partner more than ~30 ms - 650 ms (PD_T_SAFE_0V) */
	k_sleep(K_MSEC(660));
	/* Partner drops to vSafe0V */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_SAFE0V));

	/* Partner waits ~660 ms - 1000 ms (PD_T_SRC_RECOVER_MAX)
	 * + 0 ms - 275 ms (PD_T_SRC_TURN_ON)
	 */
	k_sleep(K_MSEC(660));

	/* Partner sets VBUS present */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 5000);
	zassert_ok(
		tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_PRESENT));

	/* Wait for everything to settle, and check state. */
	k_sleep(K_SECONDS(5));
	state = get_state_tc(TEST_USB_PORT);
	zassert_equal(state, /* TC_ATTACHED_SNK */ 7, "Got %d", state);
	state = get_state_pe(TEST_USB_PORT);
	zassert_equal(state, PE_SNK_READY, "Got %d", state);
}

/**
 * @brief TestPurpose: Perform a hard reset where the VBUS present is late.
 */
ZTEST_F(usb_hard_reset_source, test_vbus_present_late)
{
	int state;

	/* Partner sends the Hard Reset. */
	tcpci_partner_common_send_hard_reset(&fixture->partner_emul);
	/* Partner waits ~30 ms - 650 ms (PD_T_SAFE_0V) */
	k_sleep(K_MSEC(30));
	/* Partner drops to vSafe0V */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_SAFE0V));

	/* Partner waits more than ~660 ms - 1000 ms (PD_T_SRC_RECOVER_MAX)
	 * + 0 ms - 275 ms (PD_T_SRC_TURN_ON)
	 */
	k_sleep(K_MSEC(1300));

	/* Partner sets VBUS present */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 5000);
	zassert_ok(
		tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_PRESENT));

	/* Wait for everything to settle, and check state. */
	k_sleep(K_SECONDS(5));
	state = get_state_tc(TEST_USB_PORT);
	zassert_equal(state, /* TC_ATTACHED_SNK */ 7, "Got %d", state);
	state = get_state_pe(TEST_USB_PORT);
	zassert_equal(state, PE_SNK_READY, "Got %d", state);
}

/**
 * @brief Stop sinking when a hard reset starts, before the Source drops
 *        VBUS.
 */
ZTEST_F(usb_hard_reset_source, test_sink_path_off_before_vbus_drop)
{
	zassert_equal(charge_manager_get_active_charge_port(), TEST_USB_PORT);

	/* Partner sends the Hard Reset. */
	tcpci_partner_common_send_hard_reset(&fixture->partner_emul);
	/* Check before the partner may drop VBUS (tPSHardReset min, 25 ms). */
	k_sleep(K_MSEC(20));

	zassert_equal(charge_manager_get_active_charge_port(),
		      CHARGE_PORT_NONE);
}

struct usb_hard_reset_non_pd_source_fixture {
	struct tcpci_partner_data partner_emul;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *usb_hard_reset_non_pd_source_setup(void)
{
	static struct usb_hard_reset_non_pd_source_fixture fixture;

	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);

	return &fixture;
}

static void usb_hard_reset_non_pd_source_before(void *data)
{
	struct usb_hard_reset_non_pd_source_fixture *fixture = data;

	/* A Type-C-only source: Rp 3.0 A and VBUS, but no PD. */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = NULL;
	fixture->partner_emul.send_goodcrc = false;
	fixture->partner_emul.power_role = PD_ROLE_SOURCE;
	fixture->partner_emul.cc1 = TYPEC_CC_VOLT_RP_3_0;
	fixture->partner_emul.cc2 = TYPEC_CC_VOLT_OPEN;
	fixture->partner_emul.polarity = POLARITY_CC1;

	set_ac_enabled(true);
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 5000);
	zassert_ok(
		tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_PRESENT));
	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner_emul,
						  fixture->tcpci_emul));
	tcpci_partner_common_enable_pd_logging(&fixture->partner_emul, true);
}

static void usb_hard_reset_non_pd_source_after(void *data)
{
	struct usb_hard_reset_non_pd_source_fixture *fixture = data;

	tcpci_partner_common_enable_pd_logging(&fixture->partner_emul, false);
	tcpci_partner_common_clear_logged_msgs(&fixture->partner_emul);
	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
}

ZTEST_SUITE(usb_hard_reset_non_pd_source, drivers_predicate_post_main,
	    usb_hard_reset_non_pd_source_setup,
	    usb_hard_reset_non_pd_source_before,
	    usb_hard_reset_non_pd_source_after, NULL);

struct usb_hard_reset_no_ps_rdy_fixture {
	struct tcpci_partner_data partner_emul;
	struct tcpci_partner_extension no_ps_rdy_ext;
	struct tcpci_src_emul_data src_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

/* Accept every Request, but never send PS_RDY. */
static enum tcpci_partner_handler_res
accept_without_ps_rdy(struct tcpci_partner_extension *ext,
		      struct tcpci_partner_data *common_data,
		      const struct tcpci_emul_msg *msg)
{
	uint16_t header = sys_get_le16(msg->buf);

	if (PD_HEADER_EXT(header) || PD_HEADER_CNT(header) == 0 ||
	    PD_HEADER_TYPE(header) != PD_DATA_REQUEST)
		return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;

	tcpci_partner_stop_sender_response_timer(common_data);
	tcpci_partner_send_control_msg(common_data, PD_CTRL_ACCEPT, 0);
	return TCPCI_PARTNER_COMMON_MSG_HANDLED;
}

static struct tcpci_partner_extension_ops accept_without_ps_rdy_ops = {
	.sop_msg_handler = accept_without_ps_rdy,
};

static void *usb_hard_reset_no_ps_rdy_setup(void)
{
	static struct usb_hard_reset_no_ps_rdy_fixture fixture;

	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);

	return &fixture;
}

static void usb_hard_reset_no_ps_rdy_before(void *data)
{
	struct usb_hard_reset_no_ps_rdy_fixture *fixture = data;

	tcpci_partner_init(&fixture->partner_emul, PD_REV30);
	fixture->no_ps_rdy_ext.ops = &accept_without_ps_rdy_ops;
	fixture->no_ps_rdy_ext.next = tcpci_src_emul_init(
		&fixture->src_ext, &fixture->partner_emul, NULL);
	fixture->partner_emul.extensions = &fixture->no_ps_rdy_ext;
	tcpci_partner_common_enable_pd_logging(&fixture->partner_emul, true);
}

static void usb_hard_reset_no_ps_rdy_after(void *data)
{
	struct usb_hard_reset_no_ps_rdy_fixture *fixture = data;

	tcpci_partner_common_enable_pd_logging(&fixture->partner_emul, false);
	tcpci_partner_common_clear_logged_msgs(&fixture->partner_emul);
	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
}

ZTEST_SUITE(usb_hard_reset_no_ps_rdy, drivers_predicate_post_main,
	    usb_hard_reset_no_ps_rdy_setup, usb_hard_reset_no_ps_rdy_before,
	    usb_hard_reset_no_ps_rdy_after, NULL);

static bool hard_reset_sent(struct tcpci_partner_data *partner)
{
	struct tcpci_partner_log_msg *msg;

	SYS_SLIST_FOR_EACH_CONTAINER(&partner->msg_log, msg, node)
	{
		if (msg->sender == TCPCI_PARTNER_SENDER_TCPM &&
		    msg->sop == TCPCI_MSG_TX_HARD_RESET)
			return true;
	}
	return false;
}

/**
 * @brief Don't sink at a Requested level that the Source never confirmed with
 *        PS_RDY, including when PSTransitionTimer expiry leads to a Hard
 *        Reset.
 */
ZTEST_F(usb_hard_reset_no_ps_rdy, test_no_contract_current)
{
	/* Attach a Source that accepts the Request but never sends PS_RDY. */
	set_ac_enabled(true);
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 5000);
	zassert_ok(
		tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_PRESENT));
	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner_emul,
						  fixture->tcpci_emul));

	/*
	 * Cover attach, Source_Capabilities, Request and Accept, then
	 * PSTransitionTimer expiry and the Hard Reset that follows.
	 */
	for (int ms = 0; ms < 5000; ms += 5) {
		k_sleep(K_MSEC(5));
		zassert_not_equal(charge_manager_get_supplier(),
				  CHARGE_SUPPLIER_PD,
				  "Sinking PD current at +%d ms", ms);
	}

	zassert_true(hard_reset_sent(&fixture->partner_emul));
}

/**
 * @brief Keep sinking from a non-PD source through a Hard Reset, since it
 *        will not remove VBUS.
 */
ZTEST_F(usb_hard_reset_non_pd_source, test_keep_sinking)
{
	/* Let the attach select this port, before SinkWaitCapTimer expires. */
	k_sleep(K_MSEC(1200));
	zassert_equal(charge_manager_get_active_charge_port(), TEST_USB_PORT);
	zassert_false(hard_reset_sent(&fixture->partner_emul));

	/*
	 * Cover SinkWaitCapTimer expiry, the DUT's Hard Resets, and the
	 * tSafe0V waits for a VBUS drop that never comes.
	 */
	for (int ms = 0; ms < 4000; ms += 5) {
		k_sleep(K_MSEC(5));
		zassert_equal(charge_manager_get_active_charge_port(),
			      TEST_USB_PORT, "Lost charge port at +%d ms", ms);
	}

	zassert_true(hard_reset_sent(&fixture->partner_emul));
}
