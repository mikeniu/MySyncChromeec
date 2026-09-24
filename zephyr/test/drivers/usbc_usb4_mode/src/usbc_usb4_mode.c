/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "host_command.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_mode.h"
#include "usb_pd.h"
#include "usb_pd_tbt.h"
#include "usb_pd_vdo.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tbt_alt_mode.h"

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define TEST_PORT USBC_PORT_C0
/* Remove polarity for any mux checks */
#define USB_MUX_CHECK_MASK ~USB_PD_MUX_POLARITY_INVERTED

struct usbc_usb4_mode_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

/* Passive USB4 cable */
struct tcpci_cable_data passive_usb4 = {
	.identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT),
	.identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_PCABLE,
		/* modal operation */ false, USB_VID_GOOGLE),
	.identity_vdm[VDO_INDEX_CSTAT] = 0,
	.identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0xABCD),
	.identity_vdm[VDO_INDEX_PTYPE_CABLE1] =
		VDO_REV30_PASSIVE(USB_R30_SS_U40_GEN3, USB_VBUS_CUR_3A,
				  USB_REV30_LATENCY_1m, USB_REV30_TYPE_C),
	.identity_vdos = VDO_INDEX_PTYPE_CABLE1 + 1,

};

static void add_sop_vdm_responses(struct tcpci_partner_data *partner)
{
	/* Add Discover Identity response */
	partner->identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT);
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH_REV30(
		/* USB host */ false, /* USB device */ true,
		/* ptype_u */ IDH_PTYPE_HUB, /* modal */ false,
		/* ptype_d */ IDH_PTYPE_UNDEF, /* ctype */ USB_TYPEC_RECEPTACLE,
		USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_CSTAT] = 0;
	partner->identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0x5678);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		/* capability */ (VDO_UFP1_CAPABILITY_USB20 |
				  VDO_UFP1_CAPABILITY_USB32 |
				  VDO_UFP1_CAPABILITY_USB4),
		/* ctype */ USB_TYPEC_RECEPTACLE,
		/* alt modes */ VDO_UFP1_ALT_MODE_TBT3,
		/* speed */ USB_R30_SS_U40_GEN3);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP2_VDO] = 0;
	partner->identity_vdos = VDO_INDEX_PTYPE_UFP2_VDO + 1;

	/* Add Discover SVIDs response */
	/*
	 * TODO(b/260095516): USB4 entry does not depend on the contents of
	 * Discover SVIDs, but a valid Discover SVID response needs to to exist
	 * to ensure that discovery completes as that's a dependency in the DPM
	 * module to attempt either Enter_USB or DATA_RESET.
	 */
	partner->svids_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_SVID);
	partner->svids_vdm[VDO_INDEX_HDR + 1] = VDO_SVID(USB_VID_INTEL, 0);
	partner->svids_vdos = VDO_INDEX_HDR + 2;

	/* Add Discover Modes response */
	/*
	 * TODO(b/260095516): USB4 entry does not depend on the contents of
	 * Discover Modes, but a valid Discover Modes response needs to to exist
	 * to ensure that discovery completes as that's a dependency in the DPM
	 * module to attempt either Enter_USB or DATA_RESET.
	 */
	/* Support one mode for TBT (Intel) VID */
	partner->modes_vdm[VDO_INDEX_HDR] =
		VDO(USB_VID_INTEL, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_MODES);
	partner->modes_vdm[VDO_INDEX_HDR + 1] = TBT_ALTERNATE_MODE;
	partner->modes_vdos = VDO_INDEX_HDR + 2;
}

static void verify_cable_found(struct tcpci_cable_data *cable)
{
	uint8_t response_buffer[EC_LPC_HOST_PACKET_SIZE];
	struct ec_response_typec_discovery *discovery =
		(struct ec_response_typec_discovery *)response_buffer;

	host_cmd_typec_discovery(TEST_PORT, TYPEC_PARTNER_SOP_PRIME,
				 response_buffer, sizeof(response_buffer));

	/* The host command does not count the VDM header in identity_count. */
	zassert_equal(discovery->identity_count, cable->identity_vdos - 1,
		      "Expected %d identity VDOs, got %d",
		      cable->identity_vdos - 1, discovery->identity_count);
	zassert_mem_equal(discovery->discovery_vdo, cable->identity_vdm + 1,
			  discovery->identity_count *
				  sizeof(*discovery->discovery_vdo),
			  "Discovered SOP' identity ACK did not match");
}

static void *usbc_usb4_mode_setup(void)
{
	static struct usbc_usb4_mode_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_snk_emul_data *snk_ext = &fixture.snk_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_snk_emul_init(snk_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	add_sop_vdm_responses(partner);
	/* Note: cable behavior will vary by test case */

	return &fixture;
}

static void usbc_usb4_mode_before(void *data)
{
	struct usbc_usb4_mode_fixture *fix = data;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give TCPM time to spin */
	k_sleep(K_SECONDS(1));

	/* Enable message logging after TCPM spin */
	tcpci_partner_common_enable_pd_logging(&fix->partner, true);

	/* Initialize parter port Enter_USB msg accept/reject state */
	fix->partner.enter_usb_accept = false;
}

static void usbc_usb4_mode_after(void *data)
{
	struct usbc_usb4_mode_fixture *fix = data;

	disconnect_sink_from_port(fix->tcpci_emul);
	tcpci_partner_common_enable_pd_logging(&fix->partner, false);
	tcpci_partner_common_clear_logged_msgs(&fix->partner);
}

ZTEST_F(usbc_usb4_mode, test_discovery)
{
	uint8_t response_buffer[EC_LPC_HOST_PACKET_SIZE];
	struct ec_response_typec_discovery *discovery =
		(struct ec_response_typec_discovery *)response_buffer;

	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	host_cmd_typec_discovery(TEST_PORT, TYPEC_PARTNER_SOP, response_buffer,
				 sizeof(response_buffer));

	/* The host command does not count the VDM header in identity_count. */
	zassert_equal(discovery->identity_count,
		      fixture->partner.identity_vdos - 1,
		      "Expected %d identity VDOs, got %d",
		      fixture->partner.identity_vdos - 1,
		      discovery->identity_count);
	zassert_mem_equal(
		discovery->discovery_vdo, fixture->partner.identity_vdm + 1,
		discovery->identity_count * sizeof(*discovery->discovery_vdo),
		"Discovered SOP identity ACK did not match");
}

/* Without an e-marked cable, USB4 mode cannot be entered */
ZTEST_F(usbc_usb4_mode, test_usb4_entry_fail)
{
	struct ec_response_typec_status status;

	fixture->partner.cable = NULL;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_USB4);
	k_sleep(K_SECONDS(1));

	/*
	 * TODO(b/260095516): Notify the AP that the enter mode request
	 * failed.
	 */

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB still set");
	zassert_not_equal((status.mux_state & USB_MUX_CHECK_MASK),
			  USB_PD_MUX_USB4_ENABLED, "Unexpected USB4 mode set");
}

/* With passive e-marked cable, USB4 mode can be entered on SOP only */
ZTEST_F(usbc_usb4_mode, test_usb4_passive_entry_exit)
{
	struct ec_response_typec_status status;

	fixture->partner.cable = &passive_usb4;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	/* Instruct partner port to accept Enter_USB message */
	fixture->partner.enter_usb_accept = true;

	/* Verify that we properly identify a USB4 capable passive cable */
	verify_cable_found(fixture->partner.cable);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_USB4);
	k_sleep(K_SECONDS(2));

	/*
	 * TODO(b/260095516): Notify the AP that the enter mode request
	 * succeeded.
	 */

	/* Verify we entered USB4 mode */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB4_ENABLED, "Failed to see USB4 set");

	/* Exit modes now */
	host_cmd_typec_control_exit_modes(TEST_PORT);
	k_sleep(K_SECONDS(1));

	/* Verify that USB4 mode was exited by checking current mux state. */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB set");
}

/* If the partner claims to support USB4, but communication is only PD 2.0, the
 * EC should disregard a request to enter USB4 from the host.
 */
ZTEST_F(usbc_usb4_mode, test_usb4_pd2_no_entry)
{
	struct ec_response_typec_status status;

	tcpci_partner_init(&fixture->partner, PD_REV20);
	fixture->partner.cable = &passive_usb4;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	/* Instruct partner port to accept Enter_USB message */
	fixture->partner.enter_usb_accept = true;

	/* Verify that we properly identify a USB4 capable passive cable */
	verify_cable_found(fixture->partner.cable);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_USB4);
	k_sleep(K_SECONDS(1));

	/* PD 2.0 doesn't include Enter_USB, so it's not possible to enter USB4
	 * mode. A Discover Identity ACK indicating support for USB4 isn't even
	 * valid under PD 2.0. If the host nevertheless commands the EC to enter
	 * USB4, the EC should not attempt to do so.
	 */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB still set");
	zassert_not_equal((status.mux_state & USB_MUX_CHECK_MASK),
			  USB_PD_MUX_USB4_ENABLED, "Unexpected USB4 mode set");
}

/* If the partner rejects Enter_USB, the EC should fall back to USB mode */
ZTEST_F(usbc_usb4_mode, test_usb4_partner_rejected)
{
	struct ec_response_typec_status status;

	fixture->partner.cable = &passive_usb4;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	/* Partner rejects Enter_USB */
	fixture->partner.enter_usb_accept = false;

	verify_cable_found(fixture->partner.cable);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_USB4);
	k_sleep(K_SECONDS(2));

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB still set");
	zassert_not_equal((status.mux_state & USB_MUX_CHECK_MASK),
			  USB_PD_MUX_USB4_ENABLED, "Unexpected USB4 mode set");
}

ZTEST_F(usbc_usb4_mode, test_enter_usb_cable_capabilities)
{
	struct pd_discovery *disc_sop;
	struct pd_discovery *disc_sop_prime;
	union tbt_mode_resp_cable cable_mode_resp = { 0 };

	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	disc_sop =
		pd_get_am_discovery_and_notify_access(TEST_PORT, TCPCI_MSG_SOP);
	disc_sop_prime = pd_get_am_discovery_and_notify_access(
		TEST_PORT, TCPCI_MSG_SOP_PRIME);

	/* Baseline passive USB4 cable: capable */
	zassert_true(enter_usb_cable_is_capable(TEST_PORT));

	/* Non-e-marked cable: not capable */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_UNDEF;
	zassert_false(enter_usb_cable_is_capable(TEST_PORT));

	/* Passive cable with speed < Gen1: not capable */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_PCABLE;
	disc_sop_prime->identity.product_t1.p_rev30.ss = USB_R30_SS_U2_ONLY;
	zassert_false(enter_usb_cable_is_capable(TEST_PORT));

	/* Active cable with SVDM 2.0+ and VDO 1.3+: USB4 not supported */
	disc_sop_prime->svdm_vers = SVDM_VER_2_0;
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_ACABLE;
	disc_sop_prime->identity.product_t1.a_rev30.vdo_ver = VDO_VERSION_1_3;
	disc_sop_prime->identity.product_t2.a2_rev30.usb_40_support =
		USB4_NOT_SUPPORTED;
	zassert_false(enter_usb_cable_is_capable(TEST_PORT));

	/* Active cable with SVDM 2.0+ and VDO 1.3+: USB4 supported */
	disc_sop_prime->identity.product_t2.a2_rev30.usb_40_support =
		USB4_SUPPORTED;
	zassert_true(enter_usb_cable_is_capable(TEST_PORT));

	/* Active cable with VDO < 1.3 */
	disc_sop_prime->identity.product_t1.a_rev30.vdo_ver = 0;

	/* No modal support: not capable */
	disc_sop->identity.idh.modal_support = 0;
	zassert_false(enter_usb_cable_is_capable(TEST_PORT));

	/* Modal support present, but Intel SVID not discovered: not capable */
	disc_sop->identity.idh.modal_support = 1;
	disc_sop_prime->svid_cnt = 0;
	zassert_false(enter_usb_cable_is_capable(TEST_PORT));

	/* Intel SVID discovered, but non-rounded support: not capable */
	disc_sop_prime->svid_cnt = 1;
	disc_sop_prime->svids[0].svid = USB_VID_INTEL;
	disc_sop_prime->svids[0].mode_cnt = 1;
	disc_sop_prime->svids[0].discovery = PD_DISC_COMPLETE;
	cable_mode_resp.tbt_rounded = TBT_GEN3_NON_ROUNDED;
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode_resp.raw_value;
	zassert_false(enter_usb_cable_is_capable(TEST_PORT));

	/* Intel SVID discovered with rounded support: capable */
	cable_mode_resp.tbt_rounded = TBT_GEN3_GEN4_ROUNDED_NON_ROUNDED;
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode_resp.raw_value;
	zassert_true(enter_usb_cable_is_capable(TEST_PORT));
}

ZTEST_F(usbc_usb4_mode, test_enter_usb_port_partner_capabilities)
{
	struct pd_discovery *disc_sop;

	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	disc_sop =
		pd_get_am_discovery_and_notify_access(TEST_PORT, TCPCI_MSG_SOP);

	/* Partner supports USB4 */
	enter_usb_init(TEST_PORT);
	zassert_true(enter_usb_port_partner_is_capable(TEST_PORT));

	/* Partner revision < PD_REV30: not capable */
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP, PD_REV20);
	zassert_false(enter_usb_port_partner_is_capable(TEST_PORT));
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP, PD_REV30);

	/* When USB4 state is INACTIVE: not capable */
	enter_usb_failed(TEST_PORT);
	zassert_false(enter_usb_port_partner_is_capable(TEST_PORT));

	/* When partner does not advertise USB4 in product VDO: not capable */
	enter_usb_init(TEST_PORT);
	disc_sop->identity.product_t1.raw_value = 0;
	zassert_false(enter_usb_port_partner_is_capable(TEST_PORT));
}

ZTEST_F(usbc_usb4_mode, test_enter_usb_state_machine)
{
	enum tcpci_msg_type tx_type;
	uint32_t payload;
	struct ec_response_typec_status status;

	fixture->partner.cable = &passive_usb4;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	enter_usb_init(TEST_PORT);
	zassert_false(enter_usb_entry_is_done(TEST_PORT));

	/* First message for passive cable transitions to USB4_ENTER_SOP */
	tx_type = TCPCI_MSG_SOP;
	payload = enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	zassert_not_equal(payload, 0);

	/* Subsequent call in USB4_ENTER_SOP sets type to SOP */
	payload = enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	zassert_not_equal(payload, 0);
	zassert_equal(tx_type, TCPCI_MSG_SOP);

	/* Partner accepts SOP Enter_USB */
	enter_usb_accepted(TEST_PORT, TCPCI_MSG_SOP);
	zassert_true(enter_usb_entry_is_done(TEST_PORT));

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB4_ENABLED);

	/* Setup next msg when active returns -1 */
	payload = enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	zassert_equal(payload, (uint32_t)-1);

	/* Calling accepted or rejected while active is a no-op */
	enter_usb_accepted(TEST_PORT, TCPCI_MSG_SOP);
	enter_usb_rejected(TEST_PORT, TCPCI_MSG_SOP);
	zassert_true(enter_usb_entry_is_done(TEST_PORT));

	/* Exit mode request resets state and mux */
	usb4_exit_mode_request(TEST_PORT);
	zassert_false(enter_usb_entry_is_done(TEST_PORT));

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED);

	/* When USB4 state is INACTIVE, setup next msg returns 0 */
	enter_usb_failed(TEST_PORT);
	payload = enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	zassert_equal(payload, 0);
}

ZTEST_F(usbc_usb4_mode, test_enter_usb_active_cable_handshake)
{
	struct pd_discovery *disc_sop_prime;
	enum tcpci_msg_type tx_type;
	uint32_t payload;

	fixture->partner.cable = &passive_usb4;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	disc_sop_prime = pd_get_am_discovery_and_notify_access(
		TEST_PORT, TCPCI_MSG_SOP_PRIME);

	/* Configure active cable with SOP'' support */
	disc_sop_prime->svdm_vers = SVDM_VER_2_0;
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_ACABLE;
	disc_sop_prime->identity.product_t1.a_rev30.vdo_ver = VDO_VERSION_1_3;
	disc_sop_prime->identity.product_t1.a_rev20.sop_p_p = 1;
	disc_sop_prime->identity.product_t2.a2_rev30.usb_40_support =
		USB4_SUPPORTED;

	enter_usb_init(TEST_PORT);

	/* USB4_START -> USB4_ENTER_SOP_PRIME */
	payload = enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	zassert_not_equal(payload, 0);
	zassert_equal(tx_type, TCPCI_MSG_SOP_PRIME);

	/* Next msg while in USB4_ENTER_SOP_PRIME */
	payload = enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	zassert_equal(tx_type, TCPCI_MSG_SOP_PRIME);

	/* SOP' accepted with sop_p_p=1 -> USB4_ENTER_SOP_PRIME_PRIME */
	enter_usb_accepted(TEST_PORT, TCPCI_MSG_SOP_PRIME);
	payload = enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	zassert_equal(tx_type, TCPCI_MSG_SOP_PRIME_PRIME);

	/* SOP'' accepted -> USB4_ENTER_SOP */
	enter_usb_accepted(TEST_PORT, TCPCI_MSG_SOP_PRIME_PRIME);
	payload = enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	zassert_equal(tx_type, TCPCI_MSG_SOP);

	/* SOP accepted -> USB4_ACTIVE */
	enter_usb_accepted(TEST_PORT, TCPCI_MSG_SOP);
	zassert_true(enter_usb_entry_is_done(TEST_PORT));

	/* Repeat for active cable without SOP'' support (sop_p_p=0) */
	disc_sop_prime->identity.product_t1.a_rev20.sop_p_p = 0;
	enter_usb_init(TEST_PORT);

	payload = enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	zassert_equal(tx_type, TCPCI_MSG_SOP_PRIME);

	/* SOP' accepted with sop_p_p=0 transitions directly to USB4_ENTER_SOP
	 */
	enter_usb_accepted(TEST_PORT, TCPCI_MSG_SOP_PRIME);
	payload = enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	zassert_equal(tx_type, TCPCI_MSG_SOP);
}

ZTEST_F(usbc_usb4_mode, test_enter_usb_rejection_and_errors)
{
	enum tcpci_msg_type tx_type;

	fixture->partner.cable = &passive_usb4;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	/* 1. Unexpected response type for passive cable triggers failure */
	enter_usb_init(TEST_PORT);
	enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	enter_usb_accepted(TEST_PORT, TCPCI_MSG_SOP_PRIME);
	zassert_true(enter_usb_entry_is_done(TEST_PORT));

	/* 2. Direct rejection in USB4_ENTER_SOP */
	enter_usb_init(TEST_PORT);
	enter_usb_setup_next_msg(TEST_PORT, &tx_type);
	enter_usb_rejected(TEST_PORT, TCPCI_MSG_SOP);
	zassert_true(enter_usb_entry_is_done(TEST_PORT));

	/* 3. Unexpected accepted call in USB4_START hits default failure */
	enter_usb_init(TEST_PORT);
	enter_usb_accepted(TEST_PORT, TCPCI_MSG_SOP);
	zassert_true(enter_usb_entry_is_done(TEST_PORT));
}

ZTEST_F(usbc_usb4_mode, test_usb4_cable_speed_and_payload)
{
	struct pd_discovery *disc_sop_prime;
	union enter_usb_data_obj eudo;
	union tbt_mode_resp_cable cable_mode = { 0 };

	fixture->partner.cable = &passive_usb4;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	disc_sop_prime = pd_get_am_discovery_and_notify_access(
		TEST_PORT, TCPCI_MSG_SOP_PRIME);

	/* Passive cable speeds */
	disc_sop_prime->identity.product_t1.p_rev30.ss =
		USB_R30_SS_U32_U40_GEN1;
	zassert_equal(get_usb4_cable_speed(TEST_PORT), USB_R30_SS_U32_U40_GEN1);

	disc_sop_prime->identity.product_t1.p_rev30.ss =
		USB_R30_SS_U32_U40_GEN2;
	zassert_true(get_usb4_cable_speed(TEST_PORT) >=
		     USB_R30_SS_U32_U40_GEN2);

	/* Active cable speed clamping */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_ACABLE;
	disc_sop_prime->identity.product_t1.a_rev30.vdo_ver = VDO_VERSION_1_3;
	disc_sop_prime->identity.product_t1.a_rev30.ss =
		USB_R30_SS_U32_U40_GEN2;
	disc_sop_prime->svid_cnt = 1;
	disc_sop_prime->svids[0].svid = USB_VID_INTEL;
	disc_sop_prime->svids[0].mode_cnt = 1;
	disc_sop_prime->svids[0].discovery = PD_DISC_COMPLETE;
	cable_mode.tbt_cable_speed = TBT_SS_TBT_GEN3;
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode.raw_value;
	zassert_equal(get_usb4_cable_speed(TEST_PORT), USB_R30_SS_U32_U40_GEN2);

	/* Active cable with vdo_ver < VDO_VERSION_1_3 returns max_usb4_speed */
	disc_sop_prime->identity.product_t1.a_rev30.vdo_ver = 0;
	zassert_equal(get_usb4_cable_speed(TEST_PORT), USB_R30_SS_U40_GEN3);
	disc_sop_prime->identity.product_t1.a_rev30.vdo_ver = VDO_VERSION_1_3;

	/* Cable current: 5A */
	disc_sop_prime->identity.product_t1.p_rev20.vbus_cur = USB_VBUS_CUR_5A;
	eudo.raw_value = get_enter_usb_msg_payload(TEST_PORT);
	zassert_equal(eudo.cable_current, USB4_CABLE_CURRENT_5A);

	/* Cable current: invalid */
	disc_sop_prime->identity.product_t1.p_rev20.vbus_cur = 0;
	eudo.raw_value = get_enter_usb_msg_payload(TEST_PORT);
	zassert_equal(eudo.cable_current, USB4_CABLE_CURRENT_INVALID);

	/* Active cable active element: Retimer vs Redriver */
	disc_sop_prime->identity.product_t2.a2_rev30.active_elem =
		ACTIVE_RETIMER;
	eudo.raw_value = get_enter_usb_msg_payload(TEST_PORT);
	zassert_equal(eudo.cable_type, CABLE_TYPE_ACTIVE_RETIMER);

	disc_sop_prime->identity.product_t2.a2_rev30.active_elem =
		ACTIVE_REDRIVER;
	eudo.raw_value = get_enter_usb_msg_payload(TEST_PORT);
	zassert_equal(eudo.cable_type, CABLE_TYPE_ACTIVE_REDRIVER);

	/* PD 2.0 active cable: Retimer vs Redriver from TBT mode VDO */
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP_PRIME, PD_REV20);
	cable_mode.retimer_type = USB_RETIMER;
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode.raw_value;
	eudo.raw_value = get_enter_usb_msg_payload(TEST_PORT);
	zassert_equal(eudo.cable_type, CABLE_TYPE_ACTIVE_RETIMER);

	cable_mode.retimer_type = 0;
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode.raw_value;
	eudo.raw_value = get_enter_usb_msg_payload(TEST_PORT);
	zassert_equal(eudo.cable_type, CABLE_TYPE_ACTIVE_REDRIVER);
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP_PRIME, PD_REV30);

	/* Passive cable with TBT active cable flag */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_PCABLE;
	disc_sop_prime->svid_cnt = 1;
	disc_sop_prime->svids[0].svid = USB_VID_INTEL;
	disc_sop_prime->svids[0].mode_cnt = 1;
	disc_sop_prime->svids[0].discovery = PD_DISC_COMPLETE;
	cable_mode.tbt_active_passive = TBT_CABLE_ACTIVE;
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode.raw_value;
	eudo.raw_value = get_enter_usb_msg_payload(TEST_PORT);
	zassert_equal(eudo.cable_type, CABLE_TYPE_ACTIVE_REDRIVER);

	/* Passive cable default */
	cable_mode.tbt_active_passive = TBT_CABLE_PASSIVE;
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode.raw_value;
	eudo.raw_value = get_enter_usb_msg_payload(TEST_PORT);
	zassert_equal(eudo.cable_type, CABLE_TYPE_PASSIVE);
}

ZTEST_SUITE(usbc_usb4_mode, drivers_predicate_post_main, usbc_usb4_mode_setup,
	    usbc_usb4_mode_before, usbc_usb4_mode_after, NULL);
