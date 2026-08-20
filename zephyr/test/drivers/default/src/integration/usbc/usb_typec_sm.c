/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "host_command.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test/usb_pe.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "usb_tc_sm.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0
#define DEFAULT_VBUS_MV 5000

#define TC_FLAGS_TS_DTS_PARTNER BIT(1)

struct usb_typec_sm_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner_src;
	struct tcpci_src_emul_data src_ext;
	struct tcpci_partner_data partner_snk;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_partner_data partner_drp;
	struct tcpci_drp_emul_data drp_ext;
	struct tcpci_partner_data partner_dts_src;
	struct tcpci_partner_data partner_audio_acc;
};

static void *usb_typec_sm_setup(void)
{
	static struct usb_typec_sm_fixture fixture;

	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	tcpci_partner_init(&fixture.partner_snk, PD_REV20);
	fixture.partner_snk.extensions = tcpci_snk_emul_init(
		&fixture.snk_ext, &fixture.partner_snk, NULL);

	tcpci_partner_init(&fixture.partner_src, PD_REV20);
	fixture.partner_src.extensions = tcpci_src_emul_init(
		&fixture.src_ext, &fixture.partner_src, NULL);

	tcpci_partner_init(&fixture.partner_drp, PD_REV20);
	fixture.partner_drp.extensions = tcpci_drp_emul_init(
		&fixture.drp_ext, &fixture.partner_drp, PD_ROLE_SINK,
		tcpci_src_emul_init(&fixture.src_ext, &fixture.partner_drp,
				    NULL),
		tcpci_snk_emul_init(&fixture.snk_ext, &fixture.partner_drp,
				    NULL));

	tcpci_partner_init(&fixture.partner_dts_src, PD_REV20);
	fixture.partner_dts_src.power_role = PD_ROLE_SOURCE;

	tcpci_partner_init(&fixture.partner_audio_acc, PD_REV20);
	fixture.partner_audio_acc.power_role = PD_ROLE_SINK;

	tcpc_config[TEST_PORT].flags |= TCPC_FLAGS_TCPCI_REV2_0;

	return &fixture;
}

static void usb_typec_sm_before(void *state)
{
	struct usb_typec_sm_fixture *fixture = state;
	const struct emul *tcpci_emul = fixture->tcpci_emul;
	const struct emul *charger_emul = fixture->charger_emul;

	tcpc_config[TEST_PORT].flags |= TCPC_FLAGS_TCPCI_REV2_0;

	isl923x_emul_set_adc_vbus(charger_emul, 0);

	zassert_ok(tcpc_config[TEST_PORT].drv->init(TEST_PORT));
	pd_set_suspend(TEST_PORT, false);

	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul));
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);
	test_set_chipset_to_s0();
	tc_try_src_override(TRY_SRC_NO_OVERRIDE);

	fixture->partner_snk.extensions = tcpci_snk_emul_init(
		&fixture->snk_ext, &fixture->partner_snk, NULL);
	fixture->partner_src.extensions = tcpci_src_emul_init(
		&fixture->src_ext, &fixture->partner_src, NULL);

	k_sleep(K_SECONDS(1));
}

static void usb_typec_sm_after(void *state)
{
	struct usb_typec_sm_fixture *fixture = state;
	const struct emul *tcpci_emul = fixture->tcpci_emul;
	const struct emul *charger_emul = fixture->charger_emul;

	tcpci_emul_disconnect_partner(tcpci_emul);
	tc_try_src_override(TRY_SRC_NO_OVERRIDE);
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);
	k_sleep(K_SECONDS(1));
}

static void connect_dts_source_partner(struct usb_typec_sm_fixture *fixture,
				       enum tcpc_cc_voltage_status cc1_val,
				       enum tcpc_cc_voltage_status cc2_val)
{
	const struct emul *tcpci_emul = fixture->tcpci_emul;
	const struct emul *charger_emul = fixture->charger_emul;
	struct tcpci_partner_data *partner = &fixture->partner_dts_src;
	uint16_t power_reg_val;

	tcpci_partner_init(partner, PD_REV20);
	partner->extensions = NULL;
	partner->power_role = PD_ROLE_SOURCE;
	partner->cc1 = cc1_val;
	partner->cc2 = cc2_val;
	partner->polarity = POLARITY_CC1;

	tcpci_emul_get_reg(tcpci_emul, TCPC_REG_POWER_STATUS, &power_reg_val);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   power_reg_val | TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	zassert_ok(tcpci_partner_connect_to_tcpci(partner, tcpci_emul));
	isl923x_emul_set_adc_vbus(charger_emul, DEFAULT_VBUS_MV);

	k_sleep(K_SECONDS(2));
}

ZTEST_USER(usb_typec_sm, test_try_src_override_controls)
{
	tc_try_src_override(TRY_SRC_OVERRIDE_OFF);
	zassert_equal(tc_get_try_src_override(), TRY_SRC_OVERRIDE_OFF);

	tc_try_src_override(TRY_SRC_OVERRIDE_ON);
	zassert_equal(tc_get_try_src_override(), TRY_SRC_OVERRIDE_ON);

	tc_try_src_override(TRY_SRC_NO_OVERRIDE);
	zassert_equal(tc_get_try_src_override(), TRY_SRC_NO_OVERRIDE);
}

ZTEST_F(usb_typec_sm, test_attach_wait_snk_debug_acc_cc1_3a0_cc2_def)
{
	connect_dts_source_partner(fixture, TYPEC_CC_VOLT_RP_3_0,
				   TYPEC_CC_VOLT_RP_DEF);

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_true((tc_get_flags(TEST_PORT) & TC_FLAGS_TS_DTS_PARTNER) != 0);
	zassert_equal(tc_get_polarity(TEST_PORT), POLARITY_CC2_DTS);
}

ZTEST_F(usb_typec_sm, test_attach_wait_snk_debug_acc_cc1_1a5_cc2_def)
{
	connect_dts_source_partner(fixture, TYPEC_CC_VOLT_RP_1_5,
				   TYPEC_CC_VOLT_RP_DEF);

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_true((tc_get_flags(TEST_PORT) & TC_FLAGS_TS_DTS_PARTNER) != 0);
	zassert_equal(tc_get_polarity(TEST_PORT), POLARITY_CC2_DTS);
}

ZTEST_F(usb_typec_sm, test_attach_wait_snk_debug_acc_cc1_def_cc2_3a0)
{
	connect_dts_source_partner(fixture, TYPEC_CC_VOLT_RP_DEF,
				   TYPEC_CC_VOLT_RP_3_0);

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_true((tc_get_flags(TEST_PORT) & TC_FLAGS_TS_DTS_PARTNER) != 0);
	zassert_equal(tc_get_polarity(TEST_PORT), POLARITY_CC1_DTS);
}

ZTEST_F(usb_typec_sm, test_attach_wait_snk_debug_acc_cc1_def_cc2_1a5)
{
	connect_dts_source_partner(fixture, TYPEC_CC_VOLT_RP_DEF,
				   TYPEC_CC_VOLT_RP_1_5);

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_true((tc_get_flags(TEST_PORT) & TC_FLAGS_TS_DTS_PARTNER) != 0);
	zassert_equal(tc_get_polarity(TEST_PORT), POLARITY_CC1_DTS);
}

ZTEST_F(usb_typec_sm, test_attach_wait_snk_debug_acc_cc1_3a0_cc2_3a0)
{
	connect_dts_source_partner(fixture, TYPEC_CC_VOLT_RP_3_0,
				   TYPEC_CC_VOLT_RP_3_0);

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_true((tc_get_flags(TEST_PORT) & TC_FLAGS_TS_DTS_PARTNER) != 0);
}

ZTEST_F(usb_typec_sm, test_attach_wait_snk_debug_acc_cc1_def_cc2_def)
{
	connect_dts_source_partner(fixture, TYPEC_CC_VOLT_RP_DEF,
				   TYPEC_CC_VOLT_RP_DEF);

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_true((tc_get_flags(TEST_PORT) & TC_FLAGS_TS_DTS_PARTNER) != 0);
}

ZTEST_F(usb_typec_sm, test_attach_wait_src_audio_accessory)
{
	const struct emul *tcpci_emul = fixture->tcpci_emul;
	const struct emul *charger_emul = fixture->charger_emul;
	struct tcpci_partner_data *audio_acc = &fixture->partner_audio_acc;

	pd_set_dual_role(TEST_PORT, PD_DRP_FORCE_SOURCE);
	k_sleep(K_MSEC(100));

	tcpci_partner_init(audio_acc, PD_REV20);
	audio_acc->extensions = NULL;
	audio_acc->power_role = PD_ROLE_SINK;
	audio_acc->cc1 = TYPEC_CC_VOLT_RA;
	audio_acc->cc2 = TYPEC_CC_VOLT_RA;
	audio_acc->polarity = POLARITY_CC1;

	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	isl923x_emul_set_adc_vbus(charger_emul, 0);
	zassert_ok(tcpci_partner_connect_to_tcpci(audio_acc, tcpci_emul));

	k_sleep(K_SECONDS(1));

	zassert_false(tc_is_attached_src(TEST_PORT));
}

ZTEST_F(usb_typec_sm, test_try_src_partner_switches_to_sink)
{
	const struct emul *tcpci_emul = fixture->tcpci_emul;
	const struct emul *charger_emul = fixture->charger_emul;
	struct tcpci_partner_data *partner = &fixture->partner_drp;
	uint16_t power_reg_val;

	tc_try_src_override(TRY_SRC_OVERRIDE_ON);

	tcpci_emul_get_reg(tcpci_emul, TCPC_REG_POWER_STATUS, &power_reg_val);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   power_reg_val | TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	isl923x_emul_set_adc_vbus(charger_emul, 0);
	zassert_ok(tcpci_partner_connect_to_tcpci(partner, tcpci_emul));

	k_sleep(K_SECONDS(4));

	zassert_true(tc_is_attached_src(TEST_PORT) ||
		     tc_is_attached_snk(TEST_PORT));
}

ZTEST_F(usb_typec_sm, test_error_recovery_and_state_apis)
{
	const struct emul *tcpci_emul = fixture->tcpci_emul;
	const struct emul *charger_emul = fixture->charger_emul;
	struct tcpci_partner_data *partner = &fixture->partner_src;
	const char *state_name;
	uint16_t power_reg_val;

	tcpci_partner_init(partner, PD_REV20);
	partner->extensions =
		tcpci_src_emul_init(&fixture->src_ext, partner, NULL);

	tcpci_emul_get_reg(tcpci_emul, TCPC_REG_POWER_STATUS, &power_reg_val);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   power_reg_val | TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	zassert_ok(tcpci_partner_connect_to_tcpci(partner, tcpci_emul));
	isl923x_emul_set_adc_vbus(charger_emul, DEFAULT_VBUS_MV);

	k_sleep(K_SECONDS(2));

	state_name = tc_get_current_state(TEST_PORT);
	zassert_not_null(state_name);

	zassert_equal(tc_get_cable_plug(TEST_PORT), PD_PLUG_FROM_DFP_UFP);

	tc_update_pd_sleep_mask(TEST_PORT);
	tc_snk_power_off(TEST_PORT);
	tc_src_power_off(TEST_PORT);
	tc_set_power_role(TEST_PORT, PD_ROLE_SINK);
	tc_set_data_role(TEST_PORT, PD_ROLE_UFP);
	tc_set_msg_header_data_role(TEST_PORT, PD_ROLE_UFP);

	pd_set_error_recovery(TEST_PORT);
	k_sleep(K_SECONDS(1));
}

ZTEST_F(usb_typec_sm, test_state_machine_controls)
{
	tc_hard_reset_request(TEST_PORT);
	tc_request_power_swap(TEST_PORT);
	tc_prs_src_snk_assert_rd(TEST_PORT);
	tc_prs_snk_src_assert_rp(TEST_PORT);
	tc_pr_swap_complete(TEST_PORT, true);
	tc_pr_swap_complete(TEST_PORT, false);
	tc_usb_firmware_fw_update_run(TEST_PORT);
	tc_usb_firmware_fw_update_limited_run(TEST_PORT);
	pd_request_vconn_swap_on(TEST_PORT);
	pd_request_vconn_swap_off(TEST_PORT);
	zassert_equal(tc_check_vconn_swap(TEST_PORT), 1);
	zassert_equal(tc_is_vconn_src(TEST_PORT), 0);
}

ZTEST_SUITE(usb_typec_sm, drivers_predicate_post_main, usb_typec_sm_setup,
	    usb_typec_sm_before, usb_typec_sm_after, NULL);
