/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "host_command.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tbt.h"
#include "usb_pd_vdo.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tbt_alt_mode.h"

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/slist.h>
#include <zephyr/ztest.h>

#define TEST_PORT USBC_PORT_C0
/* Remove polarity for any mux checks */
#define USB_MUX_CHECK_MASK ~USB_PD_MUX_POLARITY_INVERTED

struct usbc_tbt_mode_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

/* Passive USB3 cable */
struct tcpci_cable_data passive_usb3 = {
	.identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT),
	.identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_PCABLE,
		/* modal operation */ false, USB_VID_GOOGLE),
	.identity_vdm[VDO_INDEX_CSTAT] = 0,
	.identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0xABCD),
	.identity_vdm[VDO_INDEX_PTYPE_CABLE1] =
		VDO_REV30_PASSIVE(USB_R30_SS_U32_U40_GEN2, USB_VBUS_CUR_3A,
				  USB_REV30_LATENCY_1m, USB_REV30_TYPE_C),
	.identity_vdos = VDO_INDEX_PTYPE_CABLE1 + 1,

};

static void add_sop_vdm_responses(struct tcpci_partner_data *partner)
{
	/* Add Discover Identity response */
	partner->identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT);
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ true, IDH_PTYPE_DFP_HUB,
		/* modal operation */ true, USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_CSTAT] = 0;
	partner->identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0x5678);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		(VDO_UFP1_CAPABILITY_USB20 | VDO_UFP1_CAPABILITY_USB32),
		USB_TYPEC_RECEPTACLE, VDO_UFP1_ALT_MODE_TBT3,
		USB_R30_SS_U40_GEN3);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP2_VDO] = 0;
	partner->identity_vdos = VDO_INDEX_PTYPE_UFP2_VDO + 1;

	/* Add Discover SVIDs response */
	/* Support TBT (Intel) VID. */
	partner->svids_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_SVID);
	partner->svids_vdm[VDO_INDEX_HDR + 1] = VDO_SVID(USB_VID_INTEL, 0);
	partner->svids_vdos = VDO_INDEX_HDR + 2;

	/* Add Discover Modes response */
	/* Support one mode for TBT (Intel) VID */
	partner->modes_vdm[VDO_INDEX_HDR] =
		VDO(USB_VID_INTEL, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_MODES);
	partner->modes_vdm[VDO_INDEX_HDR + 1] = TBT_ALTERNATE_MODE;
	partner->modes_vdos = VDO_INDEX_HDR + 2;

	/* Add affirmative mode entry */
	partner->enter_mode_vdm[VDO_INDEX_HDR] =
		VDO(USB_VID_INTEL, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_ENTER_MODE);
	partner->enter_mode_vdos = VDO_INDEX_HDR + 1;
}

/* How many EnterModes were we expecting? */
enum msg_check {
	NO_MSG,
	SOP_EXPECTED,
};

static void verify_vdm_messages(struct usbc_tbt_mode_fixture *fixture,
				enum msg_check check, int cmd_type)
{
	struct tcpci_partner_log_msg *msg;
	enum tcpci_msg_type types_seen[3];
	int messages_seen = 0;

	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->partner.msg_log, msg, node)
	{
		uint16_t header = sys_get_le16(msg->buf);

		/* Ignore messages from ourselves */
		if (msg->sender == TCPCI_PARTNER_SENDER_PARTNER) {
			continue;
		}

		/*
		 * Control messages, non-VDMs, and extended messages are not of
		 * interest
		 */
		if ((PD_HEADER_CNT(header) == 0) ||
		    (PD_HEADER_TYPE(header) != PD_DATA_VENDOR_DEF) ||
		    (PD_HEADER_EXT(header) != 0)) {
			continue;
		}

		/* We have a VDM, check entry we're interested in */
		uint32_t vdm_header = sys_get_le32(msg->buf + sizeof(header));

		if (PD_VDO_CMD(vdm_header) != cmd_type) {
			continue;
		}

		types_seen[messages_seen] = PD_HEADER_GET_SOP(header);
		messages_seen++;
	}

	/*
	 * Processing done, now verify message ordering.  See Type-C
	 * specification 6.7 Active Cables That Support Alternate Modes
	 */
	if (check == NO_MSG) {
		zassert_equal(messages_seen, 0,
			      "Unexpected messages (cmd %d, num %d)", cmd_type,
			      messages_seen);
	} else if (check == SOP_EXPECTED) {
		zassert_equal(messages_seen, 1,
			      "Unexpected messages (cmd %d, num %d)", cmd_type,
			      messages_seen);
		zassert_equal(types_seen[0], TCPCI_MSG_SOP,
			      "Unexpected SOP type: %d", types_seen[0]);
	}
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

static void *usbc_tbt_mode_setup(void)
{
	static struct usbc_tbt_mode_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_snk_emul_data *snk_ext = &fixture.snk_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_snk_emul_init(snk_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	add_sop_vdm_responses(partner);
	/* Note: cable behavior will vary by test case */

	/* Sink 5V 3A. */
	snk_ext->pdo[0] = PDO_FIXED(5000, 3000, PDO_FIXED_COMM_CAP);

	return &fixture;
}

static void usbc_tbt_mode_before(void *data)
{
	ARG_UNUSED(data);

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));
}

static void usbc_tbt_mode_after(void *data)
{
	struct usbc_tbt_mode_fixture *fix = data;

	disconnect_sink_from_port(fix->tcpci_emul);
	tcpci_partner_common_clear_logged_msgs(&fix->partner);
}

ZTEST_F(usbc_tbt_mode, test_discovery)
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
	zassert_equal(discovery->svid_count, 1, "Expected 1 SVID, got %d",
		      discovery->svid_count);
	zassert_equal(discovery->svids[0].svid, USB_VID_INTEL,
		      "Expected SVID 0x%04x, got 0x%04x", USB_VID_INTEL,
		      discovery->svids[0].svid);
	zassert_equal(discovery->svids[0].mode_count, 1,
		      "Expected 1 TBT mode, got %d",
		      discovery->svids[0].mode_count);
	zassert_equal(discovery->svids[0].mode_vdo[0],
		      fixture->partner.modes_vdm[1],
		      "TBT mode VDOs did not match");
}

/* Without an e-marked cable, TBT mode cannot be entered */
ZTEST_F(usbc_tbt_mode, test_tbt_entry_fail)
{
	struct ec_response_typec_status status;

	fixture->partner.cable = NULL;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	/* TODO(b/237553647): Test EC-driven mode entry (requires a separate
	 * config).
	 */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_TBT);
	k_sleep(K_SECONDS(1));

	/*
	 * TODO(b/168030639): Notify the AP that the enter mode request
	 * failed.
	 */

	/* Verify we refrained from sending TBT EnterMode. */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);
	verify_vdm_messages(fixture, NO_MSG, CMD_ENTER_MODE);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB still set");
	zassert_not_equal((status.mux_state & USB_MUX_CHECK_MASK),
			  USB_PD_MUX_TBT_COMPAT_ENABLED,
			  "Unexpected TBT mode set");
}

/* With passive e-marked cable, TBT mode can be entered on SOP only */
ZTEST_F(usbc_tbt_mode, test_tbt_passive_entry_exit)
{
	struct ec_response_typec_status status;

	fixture->partner.cable = &passive_usb3;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	verify_cable_found(fixture->partner.cable);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	/* TODO(b/237553647): Test EC-driven mode entry (requires a separate
	 * config).
	 */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_TBT);
	k_sleep(K_SECONDS(1));

	/*
	 * TODO(b/168030639): Notify the AP that the enter mode request
	 * succeeded.
	 */

	/* Verify we sent a single TBT SOP EnterMode. */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);
	verify_vdm_messages(fixture, SOP_EXPECTED, CMD_ENTER_MODE);
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_TBT_COMPAT_ENABLED, "Failed to see TBT set");

	/* Exit modes now */
	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_exit_modes(TEST_PORT);
	k_sleep(K_SECONDS(1));

	/* Verify we sent a single TBT SOP ExitMode. */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);
	verify_vdm_messages(fixture, SOP_EXPECTED, CMD_EXIT_MODE);
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB set");
}

ZTEST_SUITE(usbc_tbt_mode, drivers_predicate_post_main, usbc_tbt_mode_setup,
	    usbc_tbt_mode_before, usbc_tbt_mode_after, NULL);

struct usbc_tbt_mode_unit_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

static void *usbc_tbt_mode_unit_setup(void)
{
	static struct usbc_tbt_mode_unit_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_snk_emul_data *snk_ext = &fixture.snk_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_snk_emul_init(snk_ext, partner, NULL);

	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	add_sop_vdm_responses(partner);

	snk_ext->pdo[0] = PDO_FIXED(5000, 3000, PDO_FIXED_COMM_CAP);

	return &fixture;
}

static void usbc_tbt_mode_unit_before(void *data)
{
	ARG_UNUSED(data);
	test_set_chipset_to_s0();
}

static void usbc_tbt_mode_unit_after(void *data)
{
	struct usbc_tbt_mode_unit_fixture *fix = data;

	disconnect_sink_from_port(fix->tcpci_emul);
	tcpci_partner_common_clear_logged_msgs(&fix->partner);
}

static void setup_test_discovery(struct usbc_tbt_mode_unit_fixture *fix,
				 bool active_cable)
{
	struct pd_discovery *disc_sop;
	struct pd_discovery *disc_sop_prime;
	union tbt_mode_resp_cable cable_mode = {
		.tbt_active_passive = active_cable ? TBT_CABLE_ACTIVE :
						     TBT_CABLE_PASSIVE,
		.tbt_cable_speed = TBT_SS_TBT_GEN3,
		.tbt_rounded = TBT_GEN3_NON_ROUNDED,
	};

	fix->partner.cable = active_cable ? NULL : &passive_usb3;
	connect_sink_to_port(&fix->partner, fix->tcpci_emul, fix->charger_emul);

	disc_sop =
		pd_get_am_discovery_and_notify_access(TEST_PORT, TCPCI_MSG_SOP);
	disc_sop_prime = pd_get_am_discovery_and_notify_access(
		TEST_PORT, TCPCI_MSG_SOP_PRIME);

	disc_sop->identity.idh.modal_support = 1;
	disc_sop->identity.idh.product_type = IDH_PTYPE_HUB;
	disc_sop->identity_discovery = PD_DISC_COMPLETE;
	disc_sop->svid_cnt = 1;
	disc_sop->svids[0].svid = USB_VID_INTEL;
	disc_sop->svids[0].discovery = PD_DISC_COMPLETE;
	disc_sop->svids[0].mode_cnt = 1;
	disc_sop->svids[0].mode_vdo[0] = TBT_ALTERNATE_MODE;

	if (active_cable) {
		disc_sop_prime->identity.idh.product_type = IDH_PTYPE_ACABLE;
		disc_sop_prime->identity.idh.modal_support = 1;
		disc_sop_prime->identity.product_t1.a_rev20.sop_p_p = 0;
		disc_sop_prime->identity.product_t1.a_rev30.vdo_ver =
			VDO_VERSION_1_3;
		disc_sop_prime->svdm_vers = SVDM_VER_2_0;
	} else {
		disc_sop_prime->identity.idh.product_type = IDH_PTYPE_PCABLE;
		disc_sop_prime->identity.idh.modal_support = 0;
		disc_sop_prime->identity.product_t1.p_rev30.ss =
			USB_R30_SS_U32_U40_GEN2;
		disc_sop_prime->svdm_vers = SVDM_VER_2_0;
	}
	disc_sop_prime->identity_discovery = PD_DISC_COMPLETE;
	disc_sop_prime->svid_cnt = 1;
	disc_sop_prime->svids[0].svid = USB_VID_INTEL;
	disc_sop_prime->svids[0].discovery = PD_DISC_COMPLETE;
	disc_sop_prime->svids[0].mode_cnt = 1;
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode.raw_value;

	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP, PD_REV30);
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP_PRIME, PD_REV30);
}

ZTEST_F(usbc_tbt_mode_unit, test_tbt_response_valid_and_errors)
{
	int vdo_cnt = VDO_MAX_SIZE;
	uint32_t vdm[VDO_MAX_SIZE];
	enum tcpci_msg_type tx_type;

	setup_test_discovery(fixture, false);
	tbt_init(TEST_PORT);

	/* Advance state to TBT_ENTER_SOP */
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);

	/* Receiving wrong command (CMD_EXIT_MODE instead of CMD_ENTER_MODE) */
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	/* Invalid response calls tbt_exit_done -> tbt_is_active is false */
	zassert_false(tbt_is_active(TEST_PORT));

	/* Reset and re-enter TBT_ENTER_SOP */
	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);

	/* Passive cable receiving SOP' message type -> invalid for passive */
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_ENTER_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME, 1, vdm);
	zassert_false(tbt_is_active(TEST_PORT));

	/* In TBT_INACTIVE, receiving ACK should be ignored without error */
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	zassert_false(tbt_is_active(TEST_PORT));
}

ZTEST_F(usbc_tbt_mode_unit, test_tbt_vdm_naked_handling)
{
	int vdo_cnt = VDO_MAX_SIZE;
	uint32_t vdm[VDO_MAX_SIZE];
	enum tcpci_msg_type tx_type;

	/* 1. NAK during enter mode (passive cable) */
	setup_test_discovery(fixture, false);
	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
	/* In TBT_ENTER_SOP: NAK transitions to TBT_PREPARE_EXIT_MODE */
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_ENTER_MODE);

	/* Calling tbt_setup_next_vdm moves to TBT_EXIT_SOP */
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
	/* In TBT_EXIT_SOP for passive cable: NAK when retry not done retries
	 * enter mode */
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_EXIT_MODE);

	/* State is now TBT_START with RETRY_DONE set. Call setup_next_vdm */
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
	/* In TBT_ENTER_SOP again. NAK again -> TBT_PREPARE_EXIT_MODE */
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_ENTER_MODE);
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
	/* In TBT_EXIT_SOP with RETRY_DONE set: NAK gives up and calls
	 * tbt_exit_done */
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_EXIT_MODE);
	zassert_false(tbt_is_active(TEST_PORT));

	/* 2. NAK with Active Cable and SOP'' */
	setup_test_discovery(fixture, true);
	struct pd_discovery *disc_sop_prime =
		pd_get_am_discovery_and_notify_access(TEST_PORT,
						      TCPCI_MSG_SOP_PRIME);
	disc_sop_prime->identity.product_t1.a_rev20.sop_p_p = 1;

	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
	/* State is TBT_ENTER_SOP_PRIME */
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP_PRIME, CMD_ENTER_MODE);
	/* Transitions to TBT_PREPARE_EXIT_MODE */
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
	/* In TBT_EXIT_SOP. NAK with sop_p_p needed -> TBT_EXIT_SOP_PRIME_PRIME
	 */
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_EXIT_MODE);
	/* In TBT_EXIT_SOP_PRIME_PRIME. NAK -> TBT_EXIT_SOP_PRIME */
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP_PRIME_PRIME, CMD_EXIT_MODE);
	/* In TBT_EXIT_SOP_PRIME without RETRY_DONE -> tbt_retry_enter_mode */
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP_PRIME, CMD_EXIT_MODE);

	/* Now test active cable without sop_p_p (sop_prime_needed only) */
	disc_sop_prime->identity.product_t1.a_rev20.sop_p_p = 0;
	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP_PRIME, CMD_ENTER_MODE);
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
	/* In TBT_EXIT_SOP with sop_prime_needed -> TBT_EXIT_SOP_PRIME */
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_EXIT_MODE);
	/* In TBT_EXIT_SOP_PRIME: set exit_mode_request to set RETRY_DONE */
	tbt_exit_mode_request(TEST_PORT);
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP_PRIME, CMD_EXIT_MODE);
	zassert_false(tbt_is_active(TEST_PORT));

	/* 3. Unexpected NAK in unexpected state -> hits default case */
	setup_test_discovery(fixture, false);
	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	/* In TBT_ENTER_SOP */
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_ENTER_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	zassert_true(tbt_is_active(TEST_PORT));
	/* NAK in TBT_ACTIVE hits default case */
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_DISCOVER_MODES);
	zassert_false(tbt_is_active(TEST_PORT));

	/* 4. Exit Mode ACK without RETRY_DONE triggers retry */
	setup_test_discovery(fixture, false);
	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_ENTER_MODE);
	vdo_cnt = VDO_MAX_SIZE;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);

	/* Active cable without sop_p_p: ACK on TBT_EXIT_SOP_PRIME triggers
	 * retry */
	setup_test_discovery(fixture, true);
	disc_sop_prime->identity.product_t1.a_rev20.sop_p_p = 0;
	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP_PRIME, CMD_ENTER_MODE);
	vdo_cnt = VDO_MAX_SIZE;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME, 1, vdm);
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
}

ZTEST_F(usbc_tbt_mode_unit, test_tbt_vdm_acked_handling)
{
	int vdo_cnt = VDO_MAX_SIZE;
	uint32_t vdm[VDO_MAX_SIZE];
	enum tcpci_msg_type tx_type;

	/* 1. Active cable with SOP'' full entry and exit handshake */
	setup_test_discovery(fixture, true);
	struct pd_discovery *disc_sop_prime =
		pd_get_am_discovery_and_notify_access(TEST_PORT,
						      TCPCI_MSG_SOP_PRIME);
	disc_sop_prime->identity.product_t1.a_rev20.sop_p_p = 1;

	tbt_init(TEST_PORT);
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
	/* In TBT_ENTER_SOP_PRIME: setup_next_vdm formats Enter Mode SOP' */
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_SUCCESS);
	zassert_equal(tx_type, TCPCI_MSG_SOP_PRIME);
	zassert_equal(vdo_cnt, 1);
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_ENTER_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME, 1, vdm);
	/* State is now TBT_ENTER_SOP_PRIME_PRIME */
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_SUCCESS);
	zassert_equal(tx_type, TCPCI_MSG_SOP_PRIME_PRIME);

	/* ACK SOP'' enter mode */
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME_PRIME, 1, vdm);
	zassert_true(tbt_cable_entry_is_done(TEST_PORT));

	/* State is now TBT_ENTER_SOP */
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_SUCCESS);
	zassert_equal(tx_type, TCPCI_MSG_SOP);
	zassert_equal(vdo_cnt, 2);

	/* ACK SOP enter mode -> TBT_ACTIVE */
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	zassert_true(tbt_is_active(TEST_PORT));
	zassert_true(tbt_entry_is_done(TEST_PORT));

	/* Trigger exit from TBT_ACTIVE */
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_MUX_WAIT);
	/* In TBT_EXIT_SOP. Setup next VDM formats exit SOP */
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_SUCCESS);
	zassert_equal(tx_type, TCPCI_MSG_SOP);
	zassert_equal(vdo_cnt, 1);

	/* ACK exit SOP -> moves to TBT_EXIT_SOP_PRIME_PRIME */
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);

	/* Setup next VDM formats exit SOP'' */
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_SUCCESS);
	zassert_equal(tx_type, TCPCI_MSG_SOP_PRIME_PRIME);

	/* ACK exit SOP'' -> moves to TBT_EXIT_SOP_PRIME */
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME_PRIME, 1, vdm);

	/* Setup next VDM formats exit SOP' */
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_SUCCESS);
	zassert_equal(tx_type, TCPCI_MSG_SOP_PRIME);

	/* ACK exit SOP' with RETRY_DONE set -> tbt_exit_done */
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME, 1, vdm);
	zassert_false(tbt_is_active(TEST_PORT));

	/* 2. Active cable without SOP'' (sop_prime_needed only) */
	disc_sop_prime->identity.product_t1.a_rev20.sop_p_p = 0;
	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	/* In TBT_ENTER_SOP_PRIME, ACK -> directly moves to TBT_ENTER_SOP */
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_ENTER_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME, 1, vdm);
	zassert_true(tbt_cable_entry_is_done(TEST_PORT));

	/* Enter SOP -> Active */
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	/* Exit: Active -> TBT_EXIT_SOP */
	vdo_cnt = VDO_MAX_SIZE;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	/* ACK exit SOP with sop_prime_needed -> TBT_EXIT_SOP_PRIME */
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	/* ACK exit SOP' with RETRY_DONE set -> tbt_exit_done */
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME, 1, vdm);
	zassert_false(tbt_is_active(TEST_PORT));

	/* 3. Passive cable exit */
	setup_test_discovery(fixture, false);
	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_ENTER_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	/* Now active. Prepare exit mode */
	vdo_cnt = VDO_MAX_SIZE;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	/* In TBT_EXIT_SOP with RETRY_DONE set: ACK calls tbt_exit_done */
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	zassert_false(tbt_is_active(TEST_PORT));

	/* 4. Default invalid state in intel_vdm_acked and intel_vdm_naked */
	vdm[0] = VDO(USB_VID_INTEL, 1, 0);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	zassert_false(tbt_is_active(TEST_PORT));

	intel_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, 0);
	zassert_false(tbt_is_active(TEST_PORT));
}

ZTEST_F(usbc_tbt_mode_unit, test_tbt_setup_next_vdm_unsupported_and_errors)
{
	int vdo_cnt = VDO_MAX_SIZE;
	uint32_t vdm[VDO_MAX_SIZE];
	enum tcpci_msg_type tx_type;

	setup_test_discovery(fixture, false);
	struct pd_discovery *disc_sop =
		pd_get_am_discovery_and_notify_access(TEST_PORT, TCPCI_MSG_SOP);
	struct pd_discovery *disc_sop_prime =
		pd_get_am_discovery_and_notify_access(TEST_PORT,
						      TCPCI_MSG_SOP_PRIME);

	/* vdo_count < VDO_MAX_SIZE returns MSG_SETUP_ERROR */
	vdo_cnt = VDO_MAX_SIZE - 1;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_ERROR);

	/* Modal support false returns MSG_SETUP_UNSUPPORTED */
	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	disc_sop->identity.idh.modal_support = 0;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_UNSUPPORTED);
	disc_sop->identity.idh.modal_support = 1;

	/* Cable speed < U31_GEN1 returns MSG_SETUP_UNSUPPORTED */
	disc_sop_prime->identity.product_t1.p_rev30.ss = USB_R30_SS_U2_ONLY;
	disc_sop_prime->svid_cnt = 0;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_UNSUPPORTED);
	disc_sop_prime->identity.product_t1.p_rev30.ss =
		USB_R30_SS_U32_U40_GEN2;
	disc_sop_prime->svid_cnt = 1;

	/* Active cable without Intel SVID returns MSG_SETUP_UNSUPPORTED */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_ACABLE;
	disc_sop_prime->svid_cnt = 0;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_UNSUPPORTED);
	disc_sop_prime->svid_cnt = 1;
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_PCABLE;

	/* An unexpected ACK triggers tbt_exit_done */
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	zassert_false(tbt_is_active(TEST_PORT));
}

ZTEST_F(usbc_tbt_mode_unit, test_tbt_cable_speeds_and_types)
{
	setup_test_discovery(fixture, false);
	struct pd_discovery *disc_sop_prime =
		pd_get_am_discovery_and_notify_access(TEST_PORT,
						      TCPCI_MSG_SOP_PRIME);

	/* 1. Active cable is always superspeed */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_ACABLE;
	zassert_true(get_tbt_cable_speed(TEST_PORT) != TBT_SS_RES_0 ||
		     disc_sop_prime->svid_cnt > 0);

	/* 2. Undefined product type is not superspeed */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_UNDEF;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_RES_0);

	/* 3. Passive cable with PD REV3.0 speeds and no Intel mode VDO */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_PCABLE;
	disc_sop_prime->svid_cnt = 0; /* No Intel TBT mode VDO */
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP_PRIME, PD_REV30);

	disc_sop_prime->identity.product_t1.p_rev30.ss =
		USB_R30_SS_U32_U40_GEN1;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_U31_GEN1);

	disc_sop_prime->identity.product_t1.p_rev30.ss =
		USB_R30_SS_U32_U40_GEN2;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_U32_GEN1_GEN2);

	disc_sop_prime->identity.product_t1.p_rev30.ss = USB_R30_SS_U40_GEN3;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_TBT_GEN3);

	disc_sop_prime->identity.product_t1.p_rev30.ss = USB_R30_SS_U40_GEN4;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_TBT_GEN3);

	/* Invalid speed is not superspeed, returns TBT_SS_RES_0 */
	disc_sop_prime->identity.product_t1.p_rev30.ss = 0x7;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_RES_0);

	/* 4. Passive cable with PD REV2.0 */
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP_PRIME, PD_REV20);
	disc_sop_prime->identity.product_t1.p_rev20.ss = USB_R20_SS_U31_GEN1;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_U31_GEN1);

	disc_sop_prime->identity.product_t1.p_rev20.ss =
		USB_R20_SS_U31_GEN1_GEN2;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_U32_GEN1_GEN2);

	disc_sop_prime->identity.product_t1.p_rev20.ss = USB_R20_SS_U2_ONLY;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_RES_0);

	/* 5. Active cable without Intel VID returns TBT_SS_RES_0 */
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP_PRIME, PD_REV30);
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_ACABLE;
	disc_sop_prime->svid_cnt = 0;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_RES_0);

	/* 6. Cable WITH Intel mode VDO */
	disc_sop_prime->svid_cnt = 1;
	disc_sop_prime->svids[0].svid = USB_VID_INTEL;
	disc_sop_prime->svids[0].mode_cnt = 1;
	union tbt_mode_resp_cable cable_mode = {
		.tbt_cable_speed = TBT_SS_TBT_GEN3,
		.tbt_rounded = TBT_GEN3_GEN4_ROUNDED_NON_ROUNDED,
	};
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode.raw_value;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_TBT_GEN3);
	zassert_equal(get_tbt_rounded_support(TEST_PORT),
		      TBT_GEN3_GEN4_ROUNDED_NON_ROUNDED);

	/* 7. Default board_get_max_tbt_speed returns TBT_SS_TBT_GEN3 */
	zassert_equal(board_get_max_tbt_speed(TEST_PORT), TBT_SS_TBT_GEN3);

	/* 8. REV30 U32_U40_GEN1 speed in usb_rev30_to_tbt_speed */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_PCABLE;
	disc_sop_prime->svids[0].mode_vdo[0] = 0;
	disc_sop_prime->identity.product_t1.p_rev30.ss =
		USB_R30_SS_U32_U40_GEN1;
	zassert_equal(get_tbt_cable_speed(TEST_PORT), TBT_SS_U31_GEN1);
}

ZTEST_F(usbc_tbt_mode_unit, test_tbt_lrd_and_usb4_cable_entry)
{
	setup_test_discovery(fixture, false);
	struct pd_discovery *disc_sop_prime =
		pd_get_am_discovery_and_notify_access(TEST_PORT,
						      TCPCI_MSG_SOP_PRIME);

	/* LRD active cable: PCABLE in identity but ACTIVE in TBT mode */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_PCABLE;
	union tbt_mode_resp_cable cable_mode = {
		.tbt_active_passive = TBT_CABLE_ACTIVE,
		.tbt_cable_speed = TBT_SS_TBT_GEN3,
	};
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode.raw_value;

	tbt_init(TEST_PORT);
	/* Cable entry not done, LRD cable requires cable entry for USB4 */
	zassert_true(tbt_cable_entry_required_for_usb4(TEST_PORT));

	/* Normal passive cable does not require cable entry for USB4 */
	cable_mode.tbt_active_passive = TBT_CABLE_PASSIVE;
	disc_sop_prime->svids[0].mode_vdo[0] = cable_mode.raw_value;
	zassert_false(tbt_cable_entry_required_for_usb4(TEST_PORT));

	/* Active cable with SVDM < 2.0 requires cable entry for USB4 */
	disc_sop_prime->identity.idh.product_type = IDH_PTYPE_ACABLE;
	disc_sop_prime->svdm_vers = SVDM_VER_1_0;
	disc_sop_prime->identity.product_t1.a_rev30.vdo_ver = VDO_VERSION_1_3;
	zassert_true(tbt_cable_entry_required_for_usb4(TEST_PORT));

	/* Active cable with VDO ver < 1.3 requires cable entry for USB4 */
	disc_sop_prime->svdm_vers = SVDM_VER_2_0;
	disc_sop_prime->identity.product_t1.a_rev30.vdo_ver = 0;
	zassert_true(tbt_cable_entry_required_for_usb4(TEST_PORT));

	/* Active cable with SVDM 2.0 and VDO ver 1.3 does not require */
	disc_sop_prime->identity.product_t1.a_rev30.vdo_ver = VDO_VERSION_1_3;
	zassert_false(tbt_cable_entry_required_for_usb4(TEST_PORT));

	/* When tbt_cable_entry_is_done is true, returns false */
	int vdo_cnt = VDO_MAX_SIZE;
	uint32_t vdm[VDO_MAX_SIZE];
	enum tcpci_msg_type tx_type;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	/* In TBT_ENTER_SOP_PRIME, ACK to mark cable entry done */
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_ENTER_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME, 1, vdm);
	zassert_true(tbt_cable_entry_is_done(TEST_PORT));
	zassert_false(tbt_cable_entry_required_for_usb4(TEST_PORT));
}

ZTEST_F(usbc_tbt_mode_unit, test_tbt_enter_compat_mode_payload_and_exit_request)
{
	uint32_t payload[2];
	setup_test_discovery(fixture, true);
	struct pd_discovery *disc_sop_prime =
		pd_get_am_discovery_and_notify_access(TEST_PORT,
						      TCPCI_MSG_SOP_PRIME);

	/* SOP' and SOP'' return 1 object */
	zassert_equal(enter_tbt_compat_mode(TEST_PORT, TCPCI_MSG_SOP_PRIME,
					    payload),
		      1);
	zassert_equal(PD_VDO_CMD(payload[0]), CMD_ENTER_MODE);

	zassert_equal(enter_tbt_compat_mode(TEST_PORT,
					    TCPCI_MSG_SOP_PRIME_PRIME, payload),
		      1);
	zassert_equal(PD_VDO_CMD(payload[0]), CMD_ENTER_MODE);

	/* SOP returns 2 objects, with active cable flag set */
	zassert_equal(enter_tbt_compat_mode(TEST_PORT, TCPCI_MSG_SOP, payload),
		      2);
	union tbt_dev_mode_enter_cmd enter_cmd = {
		.raw_value = payload[1],
	};
	zassert_equal(enter_cmd.cable, TBT_ENTER_ACTIVE_CABLE);
	zassert_equal(enter_cmd.tbt_alt_mode, TBT_ALTERNATE_MODE);

	/* Test tbt_exit_mode_request with sop_prime_prime_needed */
	disc_sop_prime->identity.product_t1.a_rev20.sop_p_p = 1;
	tbt_init(TEST_PORT);
	int vdo_cnt = VDO_MAX_SIZE;
	uint32_t vdm[VDO_MAX_SIZE];
	enum tcpci_msg_type tx_type;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	/* Move to TBT_ENTER_SOP */
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_ENTER_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME, 1, vdm);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME_PRIME, 1, vdm);

	/* Now in TBT_ENTER_SOP. Calling tbt_exit_mode_request sets
	 * TBT_EXIT_SOP_PRIME_PRIME */
	tbt_exit_mode_request(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_SUCCESS);
	zassert_equal(tx_type, TCPCI_MSG_SOP_PRIME_PRIME);

	/* Test tbt_exit_mode_request with sop_prime_needed only */
	disc_sop_prime->identity.product_t1.a_rev20.sop_p_p = 0;
	tbt_init(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type);
	vdm[0] = VDO(USB_VID_INTEL, 1, CMD_ENTER_MODE);
	intel_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME, 1, vdm);
	tbt_exit_mode_request(TEST_PORT);
	vdo_cnt = VDO_MAX_SIZE;
	zassert_equal(tbt_setup_next_vdm(TEST_PORT, &vdo_cnt, vdm, &tx_type),
		      MSG_SETUP_SUCCESS);
	zassert_equal(tx_type, TCPCI_MSG_SOP_PRIME);

	/* Test set_tbt_compat_mode_ready */
	set_tbt_compat_mode_ready(TEST_PORT);
}

ZTEST_SUITE(usbc_tbt_mode_unit, drivers_predicate_post_main,
	    usbc_tbt_mode_unit_setup, usbc_tbt_mode_unit_before,
	    usbc_tbt_mode_unit_after, NULL);
