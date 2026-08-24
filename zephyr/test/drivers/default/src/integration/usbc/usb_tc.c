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
#include "usb_tc_sm.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0

struct usb_tc_test_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner_emul;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_src_emul_data src_ext;
};

static void *usb_tc_setup(void)
{
	static struct usb_tc_test_fixture fixture;

	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	return &fixture;
}

static void usb_tc_before(void *fixture_data)
{
	struct usb_tc_test_fixture *fixture =
		(struct usb_tc_test_fixture *)fixture_data;

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
	struct usb_tc_test_fixture *fixture =
		(struct usb_tc_test_fixture *)fixture_data;

	tcpci_emul_disconnect_partner(fixture->tcpci_emul);
	k_sleep(K_MSEC(100));
}

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

ZTEST(usb_tc, test_tc_state_queries)
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

ZTEST_SUITE(usb_tc, drivers_predicate_post_main, usb_tc_setup, usb_tc_before,
	    usb_tc_after, NULL);
