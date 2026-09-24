/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This test exercises the CONFIG_PLATFORM_EC_SVDM_RSP option,
 * verifying that the device responds appropriately to SVDM requests.
 *
 * The tests correspond to TEST.PD.PVDM.SRC.1 Discovery Process and Enter Mode
 * as defined by the USB Power Delivery Compliance Test Specification.
 */
#include "ec_commands.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tbt.h"
#include "usb_pd_vdo.h"

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

struct usbc_svdm_dfp_only_fixture {
	const struct emul *tcpci_emul;
	struct tcpci_partner_data partner;
	const struct emul *charger_emul;
	struct tcpci_src_emul_data src_emul_data;
};

struct identity_response {
	uint16_t header;
	uint32_t n_vdos;
	uint32_t vdos[7];
};

static struct identity_response
get_vdm_response(struct usbc_svdm_dfp_only_fixture *fixture, uint32_t *vdm_req,
		 size_t vdm_req_cnt)
{
	/* Send a VDM command from the partner */
	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    vdm_req, vdm_req_cnt, 0);
	k_sleep(K_SECONDS(1));
	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);

	tcpci_partner_common_print_logged_msgs(&fixture->partner);

	/* First message is the one we sent */
	zassert_not_null(sys_slist_get(&fixture->partner.msg_log),
			 "should have logged VDM request");
	/* Second should be the response */
	sys_snode_t *response_node = sys_slist_get(&fixture->partner.msg_log);

	zassert_not_null(response_node, "should have logged a PD response");

	struct tcpci_partner_log_msg *msg =
		SYS_SLIST_CONTAINER(response_node, msg, node);
	zassert_equal(msg->sop, TCPCI_MSG_SOP);

	struct identity_response out;

	out.header = sys_get_le16(msg->buf);
	out.n_vdos = (msg->cnt - sizeof(out.header)) / sizeof(*out.vdos);
	/*
	 * Header size must agree with the actual message size and fit in the
	 * vdos buffer.
	 */
	zassert_equal(PD_HEADER_CNT(out.header), out.n_vdos);
	zassert_true(out.n_vdos <= ARRAY_SIZE(out.vdos),
		     "response containing %d VDOs is too large", out.n_vdos);

	for (int i = 0; i < out.n_vdos; i++) {
		out.vdos[i] = sys_get_le32(
			&msg->buf[sizeof(*out.vdos) * i + sizeof(out.header)]);
	}

	return out;
}

/**
 * Send a Discover Identity SVDM request from the emulated partner, returning
 * the response and failing if the response has an unexpected type or size, or
 * if too few PD messages are sent.
 */
static struct identity_response
get_identity_response(struct usbc_svdm_dfp_only_fixture *fixture, int svid)
{
	uint32_t discover_identity[] = {
		VDO(svid, 1, VDO_SVDM_VERS(SVDM_VER_2_0) | CMD_DISCOVER_IDENT),
	};

	return get_vdm_response(fixture, discover_identity,
				ARRAY_SIZE(discover_identity));
}

/**
 * Verify that the header of the provided response describes a VDM response
 * for the given PD version.
 */
static void verify_response_header(const struct identity_response response,
				   enum pd_rev_type pd_rev)
{
	zassert_equal(PD_HEADER_TYPE(response.header), PD_DATA_VENDOR_DEF);
	zassert_equal(PD_HEADER_REV(response.header), pd_rev);
}

ZTEST_F(usbc_svdm_dfp_only, test_identity)
{
	fixture->partner.rev = PD_REV30;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	struct identity_response response =
		get_identity_response(fixture, USB_SID_PD);

	verify_response_header(response, PD_REV30);
	zassert_equal(response.n_vdos, 5);

	/* SVDM header: ACKing Discover_Identity */
	zassert_equal(response.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_OPOS(0) |
				  VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT),
		      "VDM Header value unexpected: %#x", response.vdos[0]);
	/* ID Header VDO per PD 3.0 */
	zassert_equal(response.vdos[1],
		      VDO_IDH_REV30(1 /* is a USB host */,
				    0 /* not a USB device */,
				    IDH_PTYPE_UNDEF /* not a UFP */,
				    0 /* no modes supported */,
				    IDH_PTYPE_DFP_HOST /* PDUSB host */,
				    USB_TYPEC_RECEPTACLE, CONFIG_USB_VID));
	/* Cert Stat VDO */
	zassert_equal(response.vdos[2], CONFIG_USB_PD_XID,
		      "Cert Stat VDO value unexpected: %#x", response.vdos[2]);
	/* Product VDO */
	zassert_equal(response.vdos[3],
		      (CONFIG_USB_PID << 16) | CONFIG_USB_BCD_DEV,
		      "Product VDO value unexpected: %#x", response.vdos[3]);
	/* DFP Product Type VDO: version 1.1, USB3.2 capable, receptacle */
	zassert_equal(response.vdos[4], 0x23800000,
		      "DFP VDO had unexpected value %#x", response.vdos[4]);
}

ZTEST_F(usbc_svdm_dfp_only, test_identity_bad_svid)
{
	fixture->partner.rev = PD_REV30;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	int svid = 0xEEEE;
	struct identity_response response =
		get_identity_response(fixture, svid);

	verify_response_header(response, PD_REV30);
	/* In PD 2.0 DFPs are required to nack a Discover Identity request */
	zassert_equal(response.n_vdos, 1);
	zassert_equal(response.vdos[0],
		      VDO(svid, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_OPOS(0) |
				  VDO_CMDT(CMDT_RSP_NAK) | CMD_DISCOVER_IDENT),
		      "VDM Header value unexpected: %#x", response.vdos[0]);
}

ZTEST_F(usbc_svdm_dfp_only, test_pd20_ufp)
{
	/* Reject DR_Swap to let DUT stay at Sink/UFP */
	tcpci_partner_set_drs_support(&fixture->partner, false, false);
	fixture->partner.rev = PD_REV20;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	struct identity_response response =
		get_identity_response(fixture, USB_SID_PD);

	verify_response_header(response, PD_REV20);
	zassert_equal(response.n_vdos, 4);

	/* SVDM header: ACKing Discover_Identity */
	zassert_equal(response.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS(SVDM_VER_1_0) | VDO_OPOS(0) |
				  VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT),
		      "VDM Header value unexpected: %#x", response.vdos[0]);
	/* ID Header VDO per PD 3.0 */
	zassert_equal(response.vdos[1],
		      VDO_IDH(1 /* is a USB host */, 0 /* not a USB device */,
			      IDH_PTYPE_UNDEF /* not a UFP */,
			      0 /* no modes supported */, CONFIG_USB_VID));
	/* Cert Stat VDO */
	zassert_equal(response.vdos[2], CONFIG_USB_PD_XID,
		      "Cert Stat VDO value unexpected: %#x", response.vdos[2]);
	/* Product VDO */
	zassert_equal(response.vdos[3],
		      (CONFIG_USB_PID << 16) | CONFIG_USB_BCD_DEV,
		      "Product VDO value unexpected: %#x", response.vdos[3]);
}

ZTEST_F(usbc_svdm_dfp_only, test_pd20_dfp_nak)
{
	fixture->partner.rev = PD_REV20;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	struct identity_response response =
		get_identity_response(fixture, USB_SID_PD);

	verify_response_header(response, PD_REV20);
	/* In PD 2.0 DFPs are required to nack a Discover Identity request */
	zassert_equal(response.n_vdos, 1);
	zassert_equal(response.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS(SVDM_VER_1_0) | VDO_OPOS(0) |
				  VDO_CMDT(CMDT_RSP_NAK) | CMD_DISCOVER_IDENT),
		      "VDM Header value unexpected: %#x", response.vdos[0]);
}

ZTEST_F(usbc_svdm_dfp_only, test_board_set_tbt_ufp_reply)
{
	zassert_equal(board_set_tbt_ufp_reply(0, TYPEC_TBT_UFP_REPLY_ACK),
		      EC_RES_SUCCESS);
	zassert_equal(board_set_tbt_ufp_reply(0, TYPEC_TBT_UFP_REPLY_NAK),
		      EC_RES_SUCCESS);
	zassert_equal(board_set_tbt_ufp_reply(0, (enum typec_tbt_ufp_reply)999),
		      EC_RES_INVALID_PARAM);
}

ZTEST_F(usbc_svdm_dfp_only, test_identity_tbt_ufp)
{
	zassert_equal(board_set_tbt_ufp_reply(0, TYPEC_TBT_UFP_REPLY_ACK),
		      EC_RES_SUCCESS);

	fixture->partner.rev = PD_REV30;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	struct identity_response response =
		get_identity_response(fixture, USB_SID_PD);

	verify_response_header(response, PD_REV30);
	zassert_equal(response.n_vdos, 7);

	/* SVDM header: ACKing Discover_Identity */
	zassert_equal(response.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_OPOS(0) |
				  VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT),
		      "VDM Header value unexpected: %#x", response.vdos[0]);
	/* ID Header VDO per PD 3.0 */
	zassert_equal(response.vdos[1],
		      VDO_IDH_REV30(1 /* is a USB host */,
				    0 /* not a USB device */,
				    IDH_PTYPE_PERIPH /* modal operation */,
				    1 /* modal operation supported */,
				    IDH_PTYPE_DFP_HOST /* PDUSB host */,
				    USB_TYPEC_RECEPTACLE, CONFIG_USB_VID));
	/* Cert Stat VDO */
	zassert_equal(response.vdos[2], CONFIG_USB_PD_XID,
		      "Cert Stat VDO value unexpected: %#x", response.vdos[2]);
	/* Product VDO */
	zassert_equal(response.vdos[3],
		      (CONFIG_USB_PID << 16) | CONFIG_USB_BCD_DEV,
		      "Product VDO value unexpected: %#x", response.vdos[3]);
	/* Product Type UFP1 VDO */
	zassert_equal(
		response.vdos[4],
		VDO_UFP1((VDO_UFP1_CAPABILITY_USB20 |
			  VDO_UFP1_CAPABILITY_USB32 | VDO_UFP1_CAPABILITY_USB4),
			 USB_TYPEC_RECEPTACLE, VDO_UFP1_ALT_MODE_TBT3,
			 USB_R30_SS_U40_GEN3),
		"Product Type UFP1 VDO unexpected: %#x", response.vdos[4]);
	/* Product Type UFP2 VDO */
	zassert_equal(response.vdos[5], 0,
		      "Product Type UFP2 VDO unexpected: %#x",
		      response.vdos[5]);
	/* DFP Product Type VDO: version 1.1, USB3.2 capable, receptacle */
	zassert_equal(response.vdos[6], 0x23800000,
		      "DFP VDO had unexpected value %#x", response.vdos[6]);
}

ZTEST_F(usbc_svdm_dfp_only, test_pd20_ufp_tbt)
{
	zassert_equal(board_set_tbt_ufp_reply(0, TYPEC_TBT_UFP_REPLY_ACK),
		      EC_RES_SUCCESS);

	/* Reject DR_Swap to let DUT stay at Sink/UFP */
	tcpci_partner_set_drs_support(&fixture->partner, false, false);
	fixture->partner.rev = PD_REV20;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	struct identity_response response =
		get_identity_response(fixture, USB_SID_PD);

	verify_response_header(response, PD_REV20);
	zassert_equal(response.n_vdos, 4);

	/* SVDM header: ACKing Discover_Identity */
	zassert_equal(response.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS(SVDM_VER_1_0) | VDO_OPOS(0) |
				  VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT),
		      "VDM Header value unexpected: %#x", response.vdos[0]);
	/* ID Header VDO with modal operation supported */
	zassert_equal(response.vdos[1],
		      VDO_IDH(1 /* is a USB host */, 0 /* not a USB device */,
			      IDH_PTYPE_PERIPH /* modal operation */,
			      1 /* modal supported */, CONFIG_USB_VID));
	/* Cert Stat VDO */
	zassert_equal(response.vdos[2], CONFIG_USB_PD_XID,
		      "Cert Stat VDO value unexpected: %#x", response.vdos[2]);
	/* Product VDO */
	zassert_equal(response.vdos[3],
		      (CONFIG_USB_PID << 16) | CONFIG_USB_BCD_DEV,
		      "Product VDO value unexpected: %#x", response.vdos[3]);
}

ZTEST_F(usbc_svdm_dfp_only, test_discover_svids)
{
	fixture->partner.rev = PD_REV30;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	uint32_t req_pd[] = {
		VDO(USB_SID_PD, 1,
		    VDO_SVDM_VERS(SVDM_VER_2_0) | CMD_DISCOVER_SVID),
	};
	uint32_t req_intel[] = {
		VDO(USB_VID_INTEL, 1,
		    VDO_SVDM_VERS(SVDM_VER_2_0) | CMD_DISCOVER_SVID),
	};

	/* Case 1: tbt_ufp_ack_allowed is false -> NAK */
	struct identity_response resp =
		get_vdm_response(fixture, req_pd, ARRAY_SIZE(req_pd));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 1);
	zassert_equal(resp.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_CMDT(CMDT_RSP_NAK) |
				  CMD_DISCOVER_SVID),
		      "VDM Header unexpected: %#x", resp.vdos[0]);

	/* Case 2: tbt_ufp_ack_allowed is true, but SVID is not USB_SID_PD ->
	 * NAK */
	zassert_equal(board_set_tbt_ufp_reply(0, TYPEC_TBT_UFP_REPLY_ACK),
		      EC_RES_SUCCESS);
	resp = get_vdm_response(fixture, req_intel, ARRAY_SIZE(req_intel));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 1);
	zassert_equal(resp.vdos[0],
		      VDO(USB_VID_INTEL, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_CMDT(CMDT_RSP_NAK) |
				  CMD_DISCOVER_SVID),
		      "VDM Header unexpected: %#x", resp.vdos[0]);

	/* Case 3: tbt_ufp_ack_allowed is true and SVID is USB_SID_PD -> ACK
	 * with Intel SVID */
	resp = get_vdm_response(fixture, req_pd, ARRAY_SIZE(req_pd));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 2);
	zassert_equal(resp.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_CMDT(CMDT_RSP_ACK) |
				  CMD_DISCOVER_SVID),
		      "VDM Header unexpected: %#x", resp.vdos[0]);
	zassert_equal(resp.vdos[1], VDO_SVID(USB_VID_INTEL, 0),
		      "SVID VDO unexpected: %#x", resp.vdos[1]);
}

ZTEST_F(usbc_svdm_dfp_only, test_discover_modes)
{
	fixture->partner.rev = PD_REV30;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	uint32_t req_intel[] = {
		VDO(USB_VID_INTEL, 1,
		    VDO_SVDM_VERS(SVDM_VER_2_0) | CMD_DISCOVER_MODES),
	};
	uint32_t req_pd[] = {
		VDO(USB_SID_PD, 1,
		    VDO_SVDM_VERS(SVDM_VER_2_0) | CMD_DISCOVER_MODES),
	};

	/* Case 1: tbt_ufp_ack_allowed is false -> NAK */
	struct identity_response resp =
		get_vdm_response(fixture, req_intel, ARRAY_SIZE(req_intel));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 1);
	zassert_equal(resp.vdos[0],
		      VDO(USB_VID_INTEL, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_CMDT(CMDT_RSP_NAK) |
				  CMD_DISCOVER_MODES),
		      "VDM Header unexpected: %#x", resp.vdos[0]);

	/* Case 2: tbt_ufp_ack_allowed is true, but SVID is not USB_VID_INTEL ->
	 * NAK */
	zassert_equal(board_set_tbt_ufp_reply(0, TYPEC_TBT_UFP_REPLY_ACK),
		      EC_RES_SUCCESS);
	resp = get_vdm_response(fixture, req_pd, ARRAY_SIZE(req_pd));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 1);
	zassert_equal(resp.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_CMDT(CMDT_RSP_NAK) |
				  CMD_DISCOVER_MODES),
		      "VDM Header unexpected: %#x", resp.vdos[0]);

	/* Case 3: tbt_ufp_ack_allowed is true and SVID is USB_VID_INTEL -> ACK
	 * with mode */
	resp = get_vdm_response(fixture, req_intel, ARRAY_SIZE(req_intel));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 2);
	zassert_equal(resp.vdos[0],
		      VDO(USB_VID_INTEL, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_CMDT(CMDT_RSP_ACK) |
				  CMD_DISCOVER_MODES),
		      "VDM Header unexpected: %#x", resp.vdos[0]);
	union tbt_mode_resp_device expected_mode = {
		.tbt_alt_mode = 0x0001,
		.tbt_adapter = TBT_ADAPTER_TBT3,
	};
	zassert_equal(resp.vdos[1], expected_mode.raw_value,
		      "Mode VDO unexpected: %#x", resp.vdos[1]);
}

ZTEST_F(usbc_svdm_dfp_only, test_enter_mode_tbt)
{
	fixture->partner.rev = PD_REV30;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	uint32_t req_enter_tbt[] = {
		VDO(USB_VID_INTEL, 1,
		    VDO_SVDM_VERS(SVDM_VER_2_0) | VDO_OPOS(1) | CMD_ENTER_MODE),
	};
	uint32_t req_wrong_opos[] = {
		VDO(USB_VID_INTEL, 1,
		    VDO_SVDM_VERS(SVDM_VER_2_0) | VDO_OPOS(2) | CMD_ENTER_MODE),
	};
	uint32_t req_wrong_vid[] = {
		VDO(USB_SID_PD, 1,
		    VDO_SVDM_VERS(SVDM_VER_2_0) | VDO_OPOS(1) | CMD_ENTER_MODE),
	};

	/* Case 1: tbt_ufp_ack_allowed is false -> NAK */
	struct identity_response resp = get_vdm_response(
		fixture, req_enter_tbt, ARRAY_SIZE(req_enter_tbt));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 1);
	zassert_equal(resp.vdos[0],
		      VDO(USB_VID_INTEL, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_OPOS(1) |
				  VDO_CMDT(CMDT_RSP_NAK) | CMD_ENTER_MODE),
		      "VDM Header unexpected: %#x", resp.vdos[0]);

	/* Enable TBT UFP */
	zassert_equal(board_set_tbt_ufp_reply(0, TYPEC_TBT_UFP_REPLY_ACK),
		      EC_RES_SUCCESS);

	/* Case 2: Wrong OPOS -> NAK */
	resp = get_vdm_response(fixture, req_wrong_opos,
				ARRAY_SIZE(req_wrong_opos));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 1);
	zassert_equal(resp.vdos[0],
		      VDO(USB_VID_INTEL, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_OPOS(2) |
				  VDO_CMDT(CMDT_RSP_NAK) | CMD_ENTER_MODE),
		      "VDM Header unexpected: %#x", resp.vdos[0]);

	/* Case 3: Wrong VID -> NAK */
	resp = get_vdm_response(fixture, req_wrong_vid,
				ARRAY_SIZE(req_wrong_vid));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 1);
	zassert_equal(resp.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_OPOS(1) |
				  VDO_CMDT(CMDT_RSP_NAK) | CMD_ENTER_MODE),
		      "VDM Header unexpected: %#x", resp.vdos[0]);

	/* Case 4: Mux not in USB/SAFE mode -> NAK */
	usb_mux_set(0, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT, POLARITY_CC1);
	resp = get_vdm_response(fixture, req_enter_tbt,
				ARRAY_SIZE(req_enter_tbt));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 1);
	zassert_equal(resp.vdos[0],
		      VDO(USB_VID_INTEL, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_OPOS(1) |
				  VDO_CMDT(CMDT_RSP_NAK) | CMD_ENTER_MODE),
		      "VDM Header unexpected: %#x", resp.vdos[0]);

	/* Case 5: Valid state -> ACK */
	usb_mux_set(0, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT,
		    POLARITY_CC1);
	resp = get_vdm_response(fixture, req_enter_tbt,
				ARRAY_SIZE(req_enter_tbt));
	verify_response_header(resp, PD_REV30);
	zassert_equal(resp.n_vdos, 1);
	zassert_equal(resp.vdos[0],
		      VDO(USB_VID_INTEL, 1,
			  VDO_SVDM_VERS(SVDM_VER_2_1) | VDO_OPOS(1) |
				  VDO_CMDT(CMDT_RSP_ACK) | CMD_ENTER_MODE),
		      "VDM Header unexpected: %#x", resp.vdos[0]);
}

static void *usbc_svdm_dfp_only_setup()
{
	static struct usbc_svdm_dfp_only_fixture fixture = {
		.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul)),
		.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul)),
	};

	tcpci_partner_init(&fixture.partner, PD_REV30);
	fixture.partner.extensions = tcpci_src_emul_init(
		&fixture.src_emul_data, &fixture.partner, NULL);
	fixture.src_emul_data.pdo[0] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	/* The first connect_source_to_port() fails if we don't do this */
	test_set_chipset_to_s0();

	return &fixture;
}

static void usbc_svdm_dfp_only_before(void *data)
{
	struct usbc_svdm_dfp_only_fixture *fixture = data;

	tcpci_partner_set_drs_support(&fixture->partner, true, true);
}

static void usbc_svdm_dfp_only_after(void *data)
{
	struct usbc_svdm_dfp_only_fixture *fixture = data;

	board_set_tbt_ufp_reply(0, TYPEC_TBT_UFP_REPLY_NAK);
	test_set_chipset_to_s0();
	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
}

ZTEST_SUITE(usbc_svdm_dfp_only, drivers_predicate_post_main,
	    usbc_svdm_dfp_only_setup, *usbc_svdm_dfp_only_before,
	    usbc_svdm_dfp_only_after, NULL);
