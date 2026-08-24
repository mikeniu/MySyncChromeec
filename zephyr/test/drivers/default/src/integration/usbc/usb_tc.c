/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test/usb_pe.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0
#define DEFAULT_VBUS_MV 5000

#if defined(TEST_BUILD) && defined(USB_PD_DEBUG_LABELS)
extern const struct test_sm_data test_tc_sm_data[];
extern const int test_tc_sm_data_size;
#endif

struct usb_tc_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner_emul;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_src_emul_data src_ext;
};

static void *usb_tc_setup(void)
{
	static struct usb_tc_fixture fixture;

	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	return &fixture;
}

static void usb_tc_before(void *fixture_data)
{
	struct usb_tc_fixture *fixture = (struct usb_tc_fixture *)fixture_data;

	/* Reset partner emulator */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = tcpci_snk_emul_init(
		&fixture->snk_ext, &fixture->partner_emul, NULL);

	zassert_ok(tcpc_config[TEST_PORT].drv->init(TEST_PORT));
	pd_set_suspend(TEST_PORT, false);
	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));

	test_set_chipset_to_s0();
	k_sleep(K_MSEC(100));
}

static void usb_tc_after(void *fixture_data)
{
	struct usb_tc_fixture *fixture = (struct usb_tc_fixture *)fixture_data;

	tcpci_emul_disconnect_partner(fixture->tcpci_emul);
	tc_try_src_override(TRY_SRC_NO_OVERRIDE);
	pd_set_suspend(TEST_PORT, false);
	k_sleep(K_MSEC(100));
}

/* =========================================================================
 * Helper and Configuration Tests
 * ========================================================================= */

ZTEST(usb_tc, test_tc_polarity_detection)
{
	/* Test CC1 Standard Polarities */
	zassert_equal(POLARITY_CC1, get_snk_polarity(TYPEC_CC_VOLT_RP_DEF,
						     TYPEC_CC_VOLT_OPEN));
	zassert_equal(POLARITY_CC1, get_snk_polarity(TYPEC_CC_VOLT_RP_1_5,
						     TYPEC_CC_VOLT_OPEN));
	zassert_equal(POLARITY_CC1, get_snk_polarity(TYPEC_CC_VOLT_RP_3_0,
						     TYPEC_CC_VOLT_OPEN));

	/* Test CC2 Standard Polarities */
	zassert_equal(POLARITY_CC2, get_snk_polarity(TYPEC_CC_VOLT_OPEN,
						     TYPEC_CC_VOLT_RP_DEF));
	zassert_equal(POLARITY_CC2, get_snk_polarity(TYPEC_CC_VOLT_OPEN,
						     TYPEC_CC_VOLT_RP_1_5));
	zassert_equal(POLARITY_CC2, get_snk_polarity(TYPEC_CC_VOLT_OPEN,
						     TYPEC_CC_VOLT_RP_3_0));

	/* Test DTS Cable Polarity on CC1 */
	zassert_equal(POLARITY_CC1_DTS, get_snk_polarity(TYPEC_CC_VOLT_RP_3_0,
							 TYPEC_CC_VOLT_RP_1_5));
	zassert_equal(POLARITY_CC1_DTS, get_snk_polarity(TYPEC_CC_VOLT_RP_1_5,
							 TYPEC_CC_VOLT_RP_DEF));
	zassert_equal(POLARITY_CC1_DTS, get_snk_polarity(TYPEC_CC_VOLT_RP_3_0,
							 TYPEC_CC_VOLT_RP_DEF));

	/* Test DTS Cable Polarity on CC2 */
	zassert_equal(POLARITY_CC2_DTS, get_snk_polarity(TYPEC_CC_VOLT_RP_1_5,
							 TYPEC_CC_VOLT_RP_3_0));
	zassert_equal(POLARITY_CC2_DTS, get_snk_polarity(TYPEC_CC_VOLT_RP_DEF,
							 TYPEC_CC_VOLT_RP_1_5));
	zassert_equal(POLARITY_CC2_DTS, get_snk_polarity(TYPEC_CC_VOLT_RP_DEF,
							 TYPEC_CC_VOLT_RP_3_0));
}

ZTEST(usb_tc, test_tc_try_src_override)
{
	enum try_src_override_t orig = tc_get_try_src_override();

	tc_try_src_override(TRY_SRC_OVERRIDE_ON);
	zassert_equal(TRY_SRC_OVERRIDE_ON, tc_get_try_src_override());

	tc_try_src_override(TRY_SRC_OVERRIDE_OFF);
	zassert_equal(TRY_SRC_OVERRIDE_OFF, tc_get_try_src_override());

	tc_try_src_override(TRY_SRC_NO_OVERRIDE);
	zassert_equal(TRY_SRC_NO_OVERRIDE, tc_get_try_src_override());

	tc_try_src_override(orig);
}

ZTEST(usb_tc, test_tc_debug_levels)
{
	tc_set_debug_level(DEBUG_DISABLE);
	tc_set_debug_level(DEBUG_LEVEL_1);
	tc_set_debug_level(DEBUG_LEVEL_2);
	tc_set_debug_level(DEBUG_LEVEL_3);
	tc_set_debug_level(DEBUG_LEVEL_1);
}

ZTEST(usb_tc, test_tc_state_and_roles)
{
	tc_pd_connection(TEST_PORT, 1);
	tc_pd_connection(TEST_PORT, 0);

	tc_set_power_role(TEST_PORT, PD_ROLE_SOURCE);
	tc_set_power_role(TEST_PORT, PD_ROLE_SINK);

	tc_set_data_role(TEST_PORT, PD_ROLE_DFP);
	tc_set_data_role(TEST_PORT, PD_ROLE_UFP);

	tc_set_msg_header_data_role(TEST_PORT, PD_ROLE_DFP);
	tc_set_msg_header_data_role(TEST_PORT, PD_ROLE_UFP);
}

ZTEST(usb_tc, test_tc_power_and_vconn_controls)
{
	tc_src_power_off(TEST_PORT);
	tc_snk_power_off(TEST_PORT);

	pd_request_vconn_swap_on(TEST_PORT);
	pd_request_vconn_swap_off(TEST_PORT);

	tc_check_vconn_swap(TEST_PORT);
	tc_is_vconn_src(TEST_PORT);

	tc_hard_reset_request(TEST_PORT);

	tc_request_power_swap(TEST_PORT);
	tc_pr_swap_complete(TEST_PORT, false);

	tc_prs_src_snk_assert_rd(TEST_PORT);
	tc_prs_snk_src_assert_rp(TEST_PORT);
}

ZTEST_USER(usb_tc, test_tc_state_queries)
{
	const char *state_name = tc_get_current_state(TEST_PORT);

	zassert_not_null(state_name);
	tc_get_flags(TEST_PORT);
	tc_get_polarity(TEST_PORT);
	tc_get_pd_enabled(TEST_PORT);
	tc_get_cable_plug(TEST_PORT);
	tc_is_attached_snk(TEST_PORT);
	tc_is_attached_src(TEST_PORT);
}

#if defined(TEST_BUILD) && defined(USB_PD_DEBUG_LABELS)
ZTEST(usb_tc, test_tc_sm_no_parent_cycles)
{
	for (int i = 0; i < test_tc_sm_data_size; ++i) {
		const struct test_sm_data *sm = &test_tc_sm_data[i];

		for (int j = 0; j < sm->size; ++j) {
			int depth = 0;
			usb_state_ptr current = &sm->base[j];

			while (current != NULL && ++depth <= sm->size) {
				current = current->parent;
			}
			zassert_true(depth <= sm->size,
				     "Cycle detected in state %d", j);
		}
	}
}

ZTEST(usb_tc, test_tc_sm_all_states_named)
{
	for (int i = 0; i < test_tc_sm_data_size; ++i) {
		const struct test_sm_data *sm = &test_tc_sm_data[i];

		for (int j = 0; j < sm->size; ++j) {
			if (sm->base[j].entry || sm->base[j].run ||
			    sm->base[j].exit) {
				zassert_true(j < sm->names_size &&
						     sm->names[j] != NULL,
					     "State %d has no name", j);
			}
		}
	}
}
#endif

/* =========================================================================
 * Sink Tests
 * ========================================================================= */

ZTEST_F(usb_tc, test_tc_attach_as_sink)
{
	/* Partner connects as Source (Rp + VBUS) -> EC attaches as Sink */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = tcpci_src_emul_init(
		&fixture->src_ext, &fixture->partner_emul, NULL);

	isl923x_emul_set_adc_vbus(fixture->charger_emul, DEFAULT_VBUS_MV);
	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner_emul,
						  fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_false(tc_is_attached_src(TEST_PORT));
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(TEST_PORT));

	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	k_sleep(K_SECONDS(1));
	zassert_false(tc_is_attached_snk(TEST_PORT));
}

ZTEST_F(usb_tc, test_tc_dts_debug_accessory_cc1)
{
	/* CC1 DTS Partner connection */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = NULL;
	fixture->partner_emul.cc1 = TYPEC_CC_VOLT_RP_1_5;
	fixture->partner_emul.cc2 = TYPEC_CC_VOLT_RP_3_0;
	fixture->partner_emul.power_role = PD_ROLE_SOURCE;
	isl923x_emul_set_adc_vbus(fixture->charger_emul, DEFAULT_VBUS_MV);

	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner_emul,
						  fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_equal(POLARITY_CC1_DTS, tc_get_polarity(TEST_PORT));

	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_SAFE0V);
	k_sleep(K_SECONDS(1));
}

ZTEST_F(usb_tc, test_tc_dts_debug_accessory_cc2)
{
	/* CC2 DTS Partner connection */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = NULL;
	fixture->partner_emul.cc1 = TYPEC_CC_VOLT_RP_3_0;
	fixture->partner_emul.cc2 = TYPEC_CC_VOLT_RP_1_5;
	fixture->partner_emul.power_role = PD_ROLE_SOURCE;
	isl923x_emul_set_adc_vbus(fixture->charger_emul, DEFAULT_VBUS_MV);

	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner_emul,
						  fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_equal(POLARITY_CC2_DTS, tc_get_polarity(TEST_PORT));

	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_SAFE0V);
	k_sleep(K_SECONDS(1));
}

ZTEST_F(usb_tc, test_tc_audio_accessory)
{
	/* Audio Accessory connection: Ra on both CC lines */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = NULL;
	fixture->partner_emul.cc1 = TYPEC_CC_VOLT_RA;
	fixture->partner_emul.cc2 = TYPEC_CC_VOLT_RA;
	fixture->partner_emul.power_role = PD_ROLE_DISCONNECTED;

	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner_emul,
						  fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));

	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));
}

/* =========================================================================
 * Source & Try.SRC Tests
 * ========================================================================= */

ZTEST_F(usb_tc, test_tc_attach_as_source)
{
	uint16_t power_reg_val;

	/* Turn on VBUS detection and safe0V */
	tcpci_emul_get_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   &power_reg_val);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   power_reg_val | TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	/* Partner connects as Sink (Rd) -> EC attaches as Source */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = tcpci_snk_emul_init(
		&fixture->snk_ext, &fixture->partner_emul, NULL);

	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner_emul,
						  fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));

	zassert_true(tc_is_attached_src(TEST_PORT));
	zassert_false(tc_is_attached_snk(TEST_PORT));
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(TEST_PORT));
	zassert_equal(POLARITY_CC2, tc_get_polarity(TEST_PORT));

	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));
	zassert_false(tc_is_attached_src(TEST_PORT));
}

ZTEST_F(usb_tc, test_tc_try_src_flow)
{
	tc_try_src_override(TRY_SRC_OVERRIDE_ON);

	/* Connect source partner with Rp */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = tcpci_src_emul_init(
		&fixture->src_ext, &fixture->partner_emul, NULL);

	isl923x_emul_set_adc_vbus(fixture->charger_emul, DEFAULT_VBUS_MV);
	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner_emul,
						  fixture->tcpci_emul));
	/* Allow Try.SRC -> TryWait.SNK -> Attached.SNK transition */
	k_sleep(K_SECONDS(2));

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_false(tc_is_attached_src(TEST_PORT));
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(TEST_PORT));

	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	tc_try_src_override(TRY_SRC_NO_OVERRIDE);
	k_sleep(K_SECONDS(1));
}

ZTEST_F(usb_tc, test_tc_try_src_disabled)
{
	tc_try_src_override(TRY_SRC_OVERRIDE_OFF);

	/* Connect source partner with Rp and VBUS */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = tcpci_src_emul_init(
		&fixture->src_ext, &fixture->partner_emul, NULL);

	isl923x_emul_set_adc_vbus(fixture->charger_emul, DEFAULT_VBUS_MV);
	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner_emul,
						  fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));

	zassert_true(tc_is_attached_snk(TEST_PORT));
	zassert_false(tc_is_attached_src(TEST_PORT));
	zassert_equal(PD_ROLE_SINK, pd_get_power_role(TEST_PORT));

	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	tc_try_src_override(TRY_SRC_NO_OVERRIDE);
	k_sleep(K_SECONDS(1));
}

ZTEST_F(usb_tc, test_tc_attached_src_try_src_detach)
{
	uint16_t power_reg_val;

	tc_try_src_override(TRY_SRC_OVERRIDE_ON);

	/* Turn on VBUS detection and safe0V */
	tcpci_emul_get_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   &power_reg_val);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   power_reg_val | TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	/* Partner connects as Sink (Rd) -> EC attaches as Source */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = tcpci_snk_emul_init(
		&fixture->snk_ext, &fixture->partner_emul, NULL);

	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner_emul,
						  fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));

	zassert_true(tc_is_attached_src(TEST_PORT));
	zassert_false(tc_is_attached_snk(TEST_PORT));
	zassert_equal(PD_ROLE_SOURCE, pd_get_power_role(TEST_PORT));

	/* Disconnect partner while Try.SRC is active (Attached.SRC ->
	 * TryWait.SNK) */
	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));
	zassert_false(tc_is_attached_src(TEST_PORT));

	tc_try_src_override(TRY_SRC_NO_OVERRIDE);
}

ZTEST_F(usb_tc, test_tc_error_recovery_and_suspend)
{
	/* Suspend port */
	pd_set_suspend(TEST_PORT, true);
	k_sleep(K_MSEC(100));

	/* Resume port */
	pd_set_suspend(TEST_PORT, false);
	k_sleep(K_MSEC(100));
}

ZTEST_SUITE(usb_tc, drivers_predicate_post_main, usb_tc_setup, usb_tc_before,
	    usb_tc_after, NULL);
