/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_dp_alt_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tbt.h"
#include "usb_pd_vdo.h"
#include "usb_prl_sm.h"
#include "usb_tbt_alt_mode.h"
#include "usbc_dp_mode.h"

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

/* Passive cable with USB3 gen 2 speed */
struct tcpci_cable_data passive_usb3_32 = {
	.identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT) |
		VDO_SVDM_VERS(SVDM_VER_2_0),
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

void add_dp_discovery(struct tcpci_partner_data *partner, int svdm_version)
{
	/* Add Discover Identity response */
	partner->identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT) |
		VDO_SVDM_VERS(svdm_version);
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ true, IDH_PTYPE_HUB,
		/* modal operation */ true, USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_CSTAT] = 0;
	partner->identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0x5678);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		(VDO_UFP1_CAPABILITY_USB20 | VDO_UFP1_CAPABILITY_USB32),
		USB_TYPEC_RECEPTACLE, VDO_UFP1_ALT_MODE_RECONFIGURE,
		USB_R30_SS_U32_U40_GEN2);
	partner->identity_vdos = VDO_INDEX_PTYPE_UFP1_VDO + 1;

	/* Add Discover Modes response */
	/* Support one mode for DisplayPort VID.*/
	partner->modes_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_MODES) |
		VDO_SVDM_VERS(svdm_version);
	partner->modes_vdm[VDO_INDEX_HDR + 1] =
		VDO_MODE_DP(MODE_DP_PIN_C | MODE_DP_PIN_D, 0, 1,
			    CABLE_RECEPTACLE, MODE_DP_GEN2, MODE_DP_SNK);
	if (svdm_version == SVDM_VER_2_1) {
		/* Applicable only for SVDM version 2.1 */
		partner->modes_vdm[VDO_INDEX_HDR + 1] |= DPAM_VER_VDO(0x1);
	}
	partner->modes_vdos = VDO_INDEX_HDR + 2;

	/* Add Discover SVIDs response */
	/* Support DisplayPort VID. */
	partner->svids_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_SVID) |
		VDO_SVDM_VERS(svdm_version);
	partner->svids_vdm[VDO_INDEX_HDR + 1] =
		VDO_SVID(USB_SID_DISPLAYPORT, 0);
	partner->svids_vdos = VDO_INDEX_HDR + 2;
}

void add_displayport_mode_responses(struct tcpci_partner_data *partner,
				    int svdm_version)
{
	/* Add DisplayPort EnterMode response */
	partner->enter_mode_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_ENTER_MODE) |
		VDO_SVDM_VERS(svdm_version);
	partner->enter_mode_vdos = VDO_INDEX_HDR + 1;

	/* Add DisplayPort StatusUpdate response */
	partner->dp_status_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DP_STATUS) |
		VDO_SVDM_VERS(svdm_version);
	partner->dp_status_vdm[VDO_INDEX_HDR + 1] =
		VDO_DP_STATUS(0, /* IRQ_HPD */
			      false, /* HPD_HI|LOW - Changed*/
			      0, /* request exit DP */
			      0, /* request exit USB */
			      1, /* MF pref */
			      true, /* DP Enabled */
			      0, /* power low e.g. normal */
			      0x2 /* Connected as Sink */);
	partner->dp_status_vdos = VDO_INDEX_HDR + 2;

	/* Add DisplayPort Configure Response */
	partner->dp_config_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DP_CONFIG) |
		VDO_SVDM_VERS(svdm_version);
	partner->dp_config_vdos = VDO_INDEX_HDR + 1;
}

static bool test_uhbr13_5_allowed = true;

bool board_is_dp_uhbr13_5_allowed(int port)
{
	return test_uhbr13_5_allowed;
}

struct usbc_dp_mode_unit_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

static void *usbc_dp_mode_unit_setup(void)
{
	static struct usbc_dp_mode_unit_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_snk_emul_data *snk_ext = &fixture.snk_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_snk_emul_init(snk_ext, partner, NULL);

	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	return &fixture;
}

static void usbc_dp_mode_unit_before(void *data)
{
	test_set_chipset_to_s0();
	test_uhbr13_5_allowed = true;
	dp_init(TEST_PORT);
}

static void usbc_dp_mode_unit_after(void *data)
{
	struct usbc_dp_mode_unit_fixture *fix = data;

	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP_PRIME, PD_REV30);
	disconnect_sink_from_port(fix->tcpci_emul);
	tcpci_partner_common_enable_pd_logging(&fix->partner, false);
	tcpci_partner_common_clear_logged_msgs(&fix->partner);
	test_set_chipset_to_s0();
	test_uhbr13_5_allowed = true;
	dp_init(TEST_PORT);
}

ZTEST_SUITE(usbc_dp_mode_unit, drivers_predicate_post_main,
	    usbc_dp_mode_unit_setup, usbc_dp_mode_unit_before,
	    usbc_dp_mode_unit_after, NULL);

static void init_sop_discovery(int port, enum usb_pd_svdm_ver svdm_ver,
			       uint32_t dp_mode_vdo)
{
	struct pd_discovery *disc =
		pd_get_am_discovery_and_notify_access(port, TCPCI_MSG_SOP);

	memset(disc, 0, sizeof(*disc));
	disc->identity.idh.modal_support = 1;
	disc->identity.idh.product_type = IDH_PTYPE_HUB;
	disc->svdm_vers = svdm_ver;
	if (dp_mode_vdo) {
		disc->svid_cnt = 1;
		disc->svids[0].svid = USB_SID_DISPLAYPORT;
		disc->svids[0].mode_cnt = 1;
		disc->svids[0].mode_vdo[0] = dp_mode_vdo;
	} else {
		disc->svid_cnt = 0;
	}
}

static void init_sop_prime_discovery(int port, enum idh_ptype ptype,
				     bool modal_support,
				     enum usb_pd_svdm_ver svdm_ver,
				     uint32_t dp_mode_vdo,
				     uint32_t tbt_mode_vdo)
{
	struct pd_discovery *disc = pd_get_am_discovery_and_notify_access(
		port, TCPCI_MSG_SOP_PRIME);
	int idx = 0;

	memset(disc, 0, sizeof(*disc));
	disc->identity.idh.modal_support = modal_support ? 1 : 0;
	disc->identity.idh.product_type = ptype;
	disc->svdm_vers = svdm_ver;

	if (dp_mode_vdo) {
		disc->svids[idx].svid = USB_SID_DISPLAYPORT;
		disc->svids[idx].mode_cnt = 1;
		disc->svids[idx].mode_vdo[0] = dp_mode_vdo;
		idx++;
	}
	if (tbt_mode_vdo) {
		disc->svids[idx].svid = USB_VID_INTEL;
		disc->svids[idx].mode_cnt = 1;
		disc->svids[idx].mode_vdo[0] = tbt_mode_vdo;
		idx++;
	}
	disc->svid_cnt = idx;
}

ZTEST_F(usbc_dp_mode_unit, test_dp_response_validation_and_failures)
{
	uint32_t vdm[VDO_MAX_SIZE];

	/* Test unexpected response from cable plug */
	dp_init(TEST_PORT);
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_ENTER_MODE);
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP_PRIME, 1, vdm);
	zassert_true(dp_is_idle(TEST_PORT));

	/* Test unexpected command in state DP_START */
	dp_init(TEST_PORT);
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_STATUS);
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	zassert_true(dp_is_idle(TEST_PORT));

	/* Test ACK while in DP_INACTIVE */
	zassert_true(dp_is_idle(TEST_PORT));
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_ENTER_MODE);
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	zassert_true(dp_is_idle(TEST_PORT));

	/* Test default / invalid state in dp_vdm_acked */
	dp_init(TEST_PORT);
	dp_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_ENTER_MODE);
	vdm[0] = 0;
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	zassert_true(dp_is_idle(TEST_PORT));
}

ZTEST_F(usbc_dp_mode_unit, test_dp_vdm_naked_handling)
{
	int cnt = VDO_MAX_SIZE;
	uint32_t vdm[VDO_MAX_SIZE];

	/* Invalid response from cable */
	dp_init(TEST_PORT);
	dp_vdm_naked(TEST_PORT, TCPCI_MSG_SOP_PRIME, CMD_ENTER_MODE);

	/* DP_START NAK -> transitions to DP_ENTER_NAKED */
	dp_init(TEST_PORT);
	dp_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_ENTER_MODE);

	/* Setup in DP_ENTER_NAKED sets DP_FLAG_RETRY and transitions to
	 * DP_PREPARE_EXIT */
	cnt = VDO_MAX_SIZE;
	zassert_equal(MSG_SETUP_MUX_WAIT,
		      dp_setup_next_vdm(TEST_PORT, &cnt, vdm));

	/* Exit mode ACK in DP_PREPARE_EXIT with retry flag sets DP_ENTER_RETRY
	 */
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_EXIT_MODE);
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);

	/* DP_ENTER_RETRY NAK -> calls dp_entry_failed */
	dp_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_ENTER_MODE);
	zassert_true(dp_is_idle(TEST_PORT));

	/* DP_PREPARE_EXIT NAK without retry flag -> dp_exit_to_usb_mode */
	dp_init(TEST_PORT);
	dp_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_ENTER_MODE);
	cnt = VDO_MAX_SIZE;
	dp_setup_next_vdm(TEST_PORT, &cnt, vdm);
	dp_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_EXIT_MODE);
	zassert_true(dp_is_idle(TEST_PORT));

	/* Default case in dp_vdm_naked: NAK during DP_ENTER_ACKED */
	dp_init(TEST_PORT);
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_ENTER_MODE);
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	dp_vdm_naked(TEST_PORT, TCPCI_MSG_SOP, CMD_DP_STATUS);
	zassert_true(dp_is_idle(TEST_PORT));
}

ZTEST_F(usbc_dp_mode_unit, test_dp_setup_next_vdm_errors)
{
	int cnt;
	uint32_t vdm[VDO_MAX_SIZE];

	/* 1. vdo_count too small */
	cnt = 0;
	zassert_equal(MSG_SETUP_ERROR, dp_setup_next_vdm(TEST_PORT, &cnt, vdm));

	/* 2. Inactive state */
	dp_init(TEST_PORT);
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_STATUS);
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	cnt = VDO_MAX_SIZE;
	zassert_equal(MSG_SETUP_ERROR, dp_setup_next_vdm(TEST_PORT, &cnt, vdm));

	/* 3. DP_START without discovered DP mode */
	dp_init(TEST_PORT);
	init_sop_discovery(TEST_PORT, SVDM_VER_2_0, 0);
	cnt = VDO_MAX_SIZE;
	zassert_equal(MSG_SETUP_ERROR, dp_setup_next_vdm(TEST_PORT, &cnt, vdm));

	/* 4. svdm_enter_dp_mode failures and suspend notifications */
	init_sop_discovery(TEST_PORT, SVDM_VER_2_0,
			   VDO_MODE_DP(MODE_DP_PIN_C | MODE_DP_PIN_D, 0, 1,
				       CABLE_RECEPTACLE, MODE_DP_GEN2,
				       MODE_DP_SNK));

	test_set_chipset_to_g3();
	dp_init(TEST_PORT);
	cnt = VDO_MAX_SIZE;
	zassert_equal(MSG_SETUP_ERROR, dp_setup_next_vdm(TEST_PORT, &cnt, vdm));
	test_set_chipset_to_s0();

	test_set_chipset_to_power_level(POWER_S3);
	zassert_equal(0, svdm_enter_dp_mode(TEST_PORT, MODE_DP_SNK));
	test_set_chipset_to_s0();
	zassert_equal(-1, svdm_enter_dp_mode(TEST_PORT, 0));

	/* 5. Pin mode is 0 in DP_STATUS_ACKED */
	dp_init(TEST_PORT);
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_ENTER_MODE);
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_STATUS);
	vdm[1] = VDO_DP_STATUS(0, 0, 0, 0, 0, 1, 0, 2);
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 2, vdm);

	init_sop_discovery(TEST_PORT, SVDM_VER_2_0,
			   VDO_MODE_DP(0, 0, 1, CABLE_RECEPTACLE, MODE_DP_GEN2,
				       MODE_DP_SNK));
	cnt = VDO_MAX_SIZE;
	zassert_equal(MSG_SETUP_ERROR, dp_setup_next_vdm(TEST_PORT, &cnt, vdm));

	/* 6. Cover line 332 in DP_PREPARE_CONFIG */
	dp_init(TEST_PORT);
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_ENTER_MODE);
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 1, vdm);
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_STATUS);
	vdm[1] = VDO_DP_STATUS(0, 0, 0, 0, 0, 1, 0, 2);
	dp_vdm_acked(TEST_PORT, TCPCI_MSG_SOP, 2, vdm);

	init_sop_discovery(TEST_PORT, SVDM_VER_2_0,
			   VDO_MODE_DP(MODE_DP_PIN_C, 0, 1, CABLE_RECEPTACLE,
				       MODE_DP_GEN2, MODE_DP_SNK));
	cnt = VDO_MAX_SIZE;
	zassert_equal(MSG_SETUP_MUX_WAIT,
		      dp_setup_next_vdm(TEST_PORT, &cnt, vdm));

	init_sop_discovery(TEST_PORT, SVDM_VER_2_0, 0);
	cnt = VDO_MAX_SIZE;
	zassert_equal(MSG_SETUP_ERROR, dp_setup_next_vdm(TEST_PORT, &cnt, vdm));
	zassert_equal(0, svdm_dp_config(TEST_PORT, vdm));
}

ZTEST_F(usbc_dp_mode_unit, test_dp_pin_modes_and_power_states)
{
	/* No DP mode discovered -> returns 0 */
	init_sop_discovery(TEST_PORT, SVDM_VER_2_0, 0);
	zassert_equal(0, pd_dfp_dp_get_pin_mode(TEST_PORT, 0));

	/* Pin caps is 0 -> returns 0 */
	init_sop_discovery(TEST_PORT, SVDM_VER_2_0,
			   VDO_MODE_DP(0, 0, 1, CABLE_RECEPTACLE, MODE_DP_GEN2,
				       MODE_DP_SNK));
	zassert_equal(0, pd_dfp_dp_get_pin_mode(TEST_PORT, 0));

	/* Pin E only */
	init_sop_discovery(TEST_PORT, SVDM_VER_2_0,
			   VDO_MODE_DP(MODE_DP_PIN_E, 0, 1, CABLE_RECEPTACLE,
				       MODE_DP_GEN2, MODE_DP_SNK));
	zassert_equal(MODE_DP_PIN_E, pd_dfp_dp_get_pin_mode(TEST_PORT, 0));

	/* Pin C and Pin E: Pin C takes precedence */
	init_sop_discovery(TEST_PORT, SVDM_VER_2_0,
			   VDO_MODE_DP(MODE_DP_PIN_C | MODE_DP_PIN_E, 0, 1,
				       CABLE_RECEPTACLE, MODE_DP_GEN2,
				       MODE_DP_SNK));
	zassert_equal(MODE_DP_PIN_C, pd_dfp_dp_get_pin_mode(TEST_PORT, 0));

	/* Multi-function preferences */
	init_sop_discovery(TEST_PORT, SVDM_VER_2_0,
			   VDO_MODE_DP(MODE_DP_PIN_C | MODE_DP_PIN_D, 0, 1,
				       CABLE_RECEPTACLE, MODE_DP_GEN2,
				       MODE_DP_SNK));
	dp_status[TEST_PORT] = VDO_DP_STATUS(0, 0, 0, 0, 0, 1, 0, 2);
	zassert_equal(MODE_DP_PIN_C,
		      pd_dfp_dp_get_pin_mode(TEST_PORT, dp_status[TEST_PORT]));
	zassert_equal(USB_PD_MUX_DP_ENABLED, svdm_dp_get_mux_mode(TEST_PORT));

	dp_status[TEST_PORT] = VDO_DP_STATUS(0, 0, 0, 0, 1, 1, 0, 2);
	zassert_equal(MODE_DP_PIN_D,
		      pd_dfp_dp_get_pin_mode(TEST_PORT, dp_status[TEST_PORT]));
	zassert_equal(USB_PD_MUX_DOCK, svdm_dp_get_mux_mode(TEST_PORT));
}

ZTEST_F(usbc_dp_mode_unit, test_dp_attention_and_hpd_handling)
{
	uint32_t payload[2];

	/* 1. Wakeup in suspend */
	test_set_chipset_to_power_level(POWER_S3);
	payload[0] = VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_STATUS);
	payload[1] = VDO_DP_STATUS(1, 1, 0, 0, 0, 1, 0, 2);
	svdm_dp_attention(TEST_PORT, payload);
	test_set_chipset_to_s0();

	/* 2. Before DP_ON: level high sets HPD_HI_PENDING */
	dp_flags[TEST_PORT] = 0;
	payload[1] = VDO_DP_STATUS(0, 1, 0, 0, 0, 1, 0, 2);
	zassert_equal(1, svdm_dp_attention(TEST_PORT, payload));
	zassert_true(dp_flags[TEST_PORT] & DP_FLAGS_HPD_HI_PENDING);

	/* 3. With DP_ON */
	dp_flags[TEST_PORT] = DP_FLAGS_DP_ON;
	payload[1] = VDO_DP_STATUS(1 /* IRQ */, 0 /* Low */, 0, 0, 0, 1, 0, 2);
	zassert_equal(1, svdm_dp_attention(TEST_PORT, payload));

	/* 4. Valid attention with HPD level high */
	payload[1] = VDO_DP_STATUS(0, 1, 0, 0, 0, 1, 0, 2);
	zassert_equal(1, svdm_dp_attention(TEST_PORT, payload));

	/* 5. Valid attention with HPD level low */
	payload[1] = VDO_DP_STATUS(0, 0, 0, 0, 0, 1, 0, 2);
	zassert_equal(1, svdm_dp_attention(TEST_PORT, payload));

	/* 6. Valid attention with IRQ and level high */
	payload[1] = VDO_DP_STATUS(1, 1, 0, 0, 0, 1, 0, 2);
	zassert_equal(1, svdm_dp_attention(TEST_PORT, payload));
}

ZTEST_F(usbc_dp_mode_unit, test_dp_cable_speeds_and_bit_rates)
{
	struct pd_discovery *disc;
	union dp_mode_resp_cable dp_cable;
	union tbt_mode_resp_cable tbt_cable;

	/* 1. board_is_dp_uhbr13_5_allowed override */
	test_uhbr13_5_allowed = false;
	zassert_false(dp_is_uhbr13_5_supported(TEST_PORT));
	test_uhbr13_5_allowed = true;

	/* 2. Passive cable PD rev 3.0 */
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_PCABLE, false,
				 SVDM_VER_2_0, 0, 0);
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP_PRIME, PD_REV30);
	disc = pd_get_am_discovery_and_notify_access(TEST_PORT,
						     TCPCI_MSG_SOP_PRIME);

	disc->identity.product_t1.p_rev30.ss = USB_R30_SS_U32_U40_GEN1;
	zassert_equal(DP_UHBR10, dp_get_cable_bit_rate(TEST_PORT));

	disc->identity.product_t1.p_rev30.ss = USB_R30_SS_U40_GEN3;
	zassert_equal(DP_UHBR20, dp_get_cable_bit_rate(TEST_PORT));

	disc->identity.product_t1.p_rev30.ss = 0;
	zassert_equal(DP_HBR3, dp_get_cable_bit_rate(TEST_PORT));

	/* 3. Passive cable PD rev 2.0 */
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP_PRIME, PD_REV20);
	disc->identity.product_t1.p_rev20.ss = USB_R20_SS_U31_GEN1;
	zassert_equal(DP_UHBR10, dp_get_cable_bit_rate(TEST_PORT));

	disc->identity.product_t1.p_rev20.ss = 0;
	zassert_equal(DP_HBR3, dp_get_cable_bit_rate(TEST_PORT));
	prl_set_rev(TEST_PORT, TCPCI_MSG_SOP_PRIME, PD_REV30);

	/* 4. Active cable / DPAM 2.1 signaling */
	dp_cable.raw_value = 0;
	dp_cable.dpam_ver = DPAM_VERSION_21;

	dp_cable.signaling = DP_UHBR20;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_ACABLE, true,
				 SVDM_VER_2_1, dp_cable.raw_value, 0);
	zassert_equal(DP_UHBR20, dp_get_cable_bit_rate(TEST_PORT));

	dp_cable.signaling = DP_UHBR10;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_ACABLE, true,
				 SVDM_VER_2_1, dp_cable.raw_value, 0);
	zassert_equal(DP_UHBR10, dp_get_cable_bit_rate(TEST_PORT));

	dp_cable.signaling = 0;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_ACABLE, true,
				 SVDM_VER_2_1, dp_cable.raw_value, 0);
	zassert_equal(DP_HBR3, dp_get_cable_bit_rate(TEST_PORT));

	/* 5. Cable with SVDM 2.1 but dpam_ver == 0 */
	dp_cable.dpam_ver = 0;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_ACABLE, true,
				 SVDM_VER_2_1, dp_cable.raw_value, 0);
	zassert_equal(DPAM_VERSION_20,
		      dp_resolve_dpam_version(TEST_PORT, TCPCI_MSG_SOP_PRIME));
	zassert_equal(DP_HBR3, dp_get_cable_bit_rate(TEST_PORT));

	/* 6. TBT cable speed */
	tbt_cable.raw_value = 0;
	tbt_cable.tbt_alt_mode = TBT_ALTERNATE_MODE;
	tbt_cable.tbt_active_passive = TBT_CABLE_PASSIVE;
	tbt_cable.retimer_type = USB_NOT_RETIMER;

	tbt_cable.tbt_cable_speed = TBT_SS_U31_GEN1;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_PCABLE, true,
				 SVDM_VER_2_0, 0, tbt_cable.raw_value);
	disc = pd_get_am_discovery_and_notify_access(TEST_PORT,
						     TCPCI_MSG_SOP_PRIME);
	disc->identity.product_t1.p_rev30.ss = USB_R30_SS_U32_U40_GEN1;
	zassert_equal(DP_UHBR10, dp_get_cable_bit_rate(TEST_PORT));

	tbt_cable.tbt_cable_speed = TBT_SS_TBT_GEN3;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_PCABLE, true,
				 SVDM_VER_2_0, 0, tbt_cable.raw_value);
	disc = pd_get_am_discovery_and_notify_access(TEST_PORT,
						     TCPCI_MSG_SOP_PRIME);
	disc->identity.product_t1.p_rev30.ss = USB_R30_SS_U32_U40_GEN1;
	zassert_equal(DP_UHBR20, dp_get_cable_bit_rate(TEST_PORT));

	tbt_cable.tbt_cable_speed = TBT_SS_RES_0;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_PCABLE, true,
				 SVDM_VER_2_0, 0, tbt_cable.raw_value);
	disc = pd_get_am_discovery_and_notify_access(TEST_PORT,
						     TCPCI_MSG_SOP_PRIME);
	disc->identity.product_t1.p_rev30.ss = USB_R30_SS_U32_U40_GEN1;
	zassert_equal(DP_HBR3, dp_get_cable_bit_rate(TEST_PORT));
}

ZTEST_F(usbc_dp_mode_unit, test_dp_cable_type_flags_and_vdo_cfg)
{
	union dp_mode_resp_cable dp_cable;
	union tbt_mode_resp_cable tbt_cable;
	struct dp_cable_type_flags flags;
	union dp_mode_cfg cfg;

	/* 1. SOP with DPAM 2.1 */
	init_sop_discovery(TEST_PORT, SVDM_VER_2_1,
			   DPAM_VER_VDO(1) | VDO_MODE_DP(MODE_DP_PIN_C, 0, 1,
							 CABLE_RECEPTACLE,
							 MODE_DP_GEN2,
							 MODE_DP_SNK));

	/* Optical cable */
	dp_cable.raw_value = 0;
	dp_cable.dpam_ver = DPAM_VERSION_21;
	dp_cable.active_comp = DP21_OPTICAL_CABLE;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_ACABLE, true,
				 SVDM_VER_2_1, dp_cable.raw_value, 0);
	flags = dp_get_pd_cable_type_flags(TEST_PORT);
	zassert_true(flags.optical);
	cfg = dp_create_vdo_cfg(TEST_PORT, MODE_DP_PIN_C);
	zassert_equal(DP21_OPTICAL_CABLE, cfg.active_comp);

	/* Retimer cable */
	dp_cable.active_comp = DP21_ACTIVE_RETIMER_CABLE;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_ACABLE, true,
				 SVDM_VER_2_1, dp_cable.raw_value, 0);
	flags = dp_get_pd_cable_type_flags(TEST_PORT);
	zassert_true(flags.active);
	zassert_true(flags.retimer);
	cfg = dp_create_vdo_cfg(TEST_PORT, MODE_DP_PIN_C);
	zassert_equal(DP21_ACTIVE_RETIMER_CABLE, cfg.active_comp);

	/* Redriver cable */
	dp_cable.active_comp = DP21_ACTIVE_REDRIVER_CABLE;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_ACABLE, true,
				 SVDM_VER_2_1, dp_cable.raw_value, 0);
	flags = dp_get_pd_cable_type_flags(TEST_PORT);
	zassert_true(flags.active);
	zassert_false(flags.retimer);
	cfg = dp_create_vdo_cfg(TEST_PORT, MODE_DP_PIN_C);
	zassert_equal(DP21_ACTIVE_REDRIVER_CABLE, cfg.active_comp);

	/* Passive cable */
	dp_cable.active_comp = DP21_PASSIVE_CABLE;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_PCABLE, false,
				 SVDM_VER_2_1, dp_cable.raw_value, 0);
	flags = dp_get_pd_cable_type_flags(TEST_PORT);
	zassert_false(flags.active);
	cfg = dp_create_vdo_cfg(TEST_PORT, MODE_DP_PIN_C);
	zassert_equal(DP21_PASSIVE_CABLE, cfg.active_comp);

	/* 2. TBT cable flags when SOP' is not DPAM 2.1 */
	tbt_cable.raw_value = 0;
	tbt_cable.tbt_alt_mode = TBT_ALTERNATE_MODE;
	tbt_cable.tbt_active_passive = TBT_CABLE_ACTIVE;
	tbt_cable.retimer_type = USB_RETIMER;
	tbt_cable.tbt_cable = TBT_CABLE_OPTICAL;
	init_sop_prime_discovery(TEST_PORT, IDH_PTYPE_ACABLE, true,
				 SVDM_VER_2_0, 0, tbt_cable.raw_value);
	flags = dp_get_pd_cable_type_flags(TEST_PORT);
	zassert_true(flags.active);
	zassert_true(flags.retimer);
	zassert_true(flags.optical);

	/* 3. SOP without DPAM 2.1 */
	init_sop_discovery(TEST_PORT, SVDM_VER_2_0,
			   VDO_MODE_DP(MODE_DP_PIN_C, 0, 1, CABLE_RECEPTACLE,
				       MODE_DP_GEN2, MODE_DP_SNK));
	cfg = dp_create_vdo_cfg(TEST_PORT, MODE_DP_PIN_C);
	zassert_equal(DP_HBR3, cfg.signaling);
}

ZTEST_F(usbc_dp_mode_unit, test_dp_console_cmd_mfallow)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "mfallow");
	zassert_equal(EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "mfallow invalid_port true");
	zassert_equal(EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "mfallow 99 true");
	zassert_equal(EC_ERROR_PARAM1, rv);

	rv = shell_execute_cmd(get_ec_shell(), "mfallow 0 invalid_bool");
	zassert_equal(EC_ERROR_PARAM2, rv);

	rv = shell_execute_cmd(get_ec_shell(), "mfallow 0 true");
	zassert_equal(EC_SUCCESS, rv);

	rv = shell_execute_cmd(get_ec_shell(), "mfallow 0 false");
	zassert_equal(EC_SUCCESS, rv);

	/* Restore to true */
	rv = shell_execute_cmd(get_ec_shell(), "mfallow 0 true");
	zassert_equal(EC_SUCCESS, rv);
}
