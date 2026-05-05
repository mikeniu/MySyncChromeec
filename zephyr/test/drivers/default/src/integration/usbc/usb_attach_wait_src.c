/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

enum usb_tc_state {
	NOT_A_REAL_STATE = -1,
};
enum usb_tc_state get_state_tc(const int port);

#define TC_UNATTACHED_SNK 5
#define TC_ATTACH_WAIT_SRC 9
#define TC_ATTACHED_SRC 10

#define TEST_PORT 0
BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

#include <zephyr/sys/atomic.h>

static atomic_t test_port_is_dts = ATOMIC_INIT(1);

bool board_is_dts_port(int port)
{
	return atomic_get(&test_port_is_dts) != 0;
}

struct attach_wait_src_fixture {
	struct tcpci_partner_data snk_partner;
	struct tcpci_snk_emul_data snk_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *attach_wait_src_setup(void)
{
	static struct attach_wait_src_fixture fixture;

	return &fixture;
}

static void attach_wait_src_before(void *state)
{
	struct attach_wait_src_fixture *fixture = state;

	fixture->tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	fixture->charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	tcpci_emul_reset(fixture->tcpci_emul);
	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassert_ok(tcpc_config[TEST_PORT].drv->init(TEST_PORT));
	pd_set_suspend(TEST_PORT, false);

	k_sleep(K_SECONDS(1));
}

static void attach_wait_src_after(void *state)
{
	struct attach_wait_src_fixture *fixture = state;

	atomic_set(&test_port_is_dts, 1);

	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	tcpci_emul_reset(fixture->tcpci_emul);
	k_sleep(K_SECONDS(3));
}

static void connect_debug_accessory(struct attach_wait_src_fixture *fixture)
{
	uint16_t power_reg_val;

	tcpci_partner_init(&fixture->snk_partner, PD_REV20);
	fixture->snk_partner.extensions = tcpci_snk_emul_init(
		&fixture->snk_ext, &fixture->snk_partner, NULL);

	fixture->snk_partner.cc2 = TYPEC_CC_VOLT_RD;

	tcpci_emul_get_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   &power_reg_val);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   power_reg_val | TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->snk_partner,
						  fixture->tcpci_emul));
}

ZTEST_F(attach_wait_src, test_dbg_acc_no_dts_force_sink)
{
	atomic_set(&test_port_is_dts, 0);

	test_set_chipset_to_s0();
	k_sleep(K_SECONDS(1));

	pd_set_dual_role(TEST_PORT, PD_DRP_FORCE_SINK);
	k_sleep(K_SECONDS(1));

	connect_debug_accessory(fixture);
	k_sleep(K_SECONDS(10));

	zassert_not_equal((int)get_state_tc(TEST_PORT), TC_ATTACHED_SRC,
			  "Port must NOT attach as SRC for Rd/Rd on "
			  "non-DTS port with FORCE_SINK, got state %d",
			  (int)get_state_tc(TEST_PORT));
}

ZTEST_F(attach_wait_src, test_dbg_acc_dts_port)
{
	test_set_chipset_to_s0();
	k_sleep(K_SECONDS(1));

	pd_set_dual_role(TEST_PORT, PD_DRP_TOGGLE_ON);
	k_sleep(K_SECONDS(1));

	connect_debug_accessory(fixture);
	k_sleep(K_SECONDS(10));

	zassert_equal((int)get_state_tc(TEST_PORT), TC_ATTACHED_SRC,
		      "Expected TC_ATTACHED_SRC (%d), got state %d",
		      TC_ATTACHED_SRC, (int)get_state_tc(TEST_PORT));
}

ZTEST_F(attach_wait_src, test_ufp_attached)
{
	uint16_t power_reg_val;

	test_set_chipset_to_s0();
	k_sleep(K_SECONDS(1));

	pd_set_dual_role(TEST_PORT, PD_DRP_TOGGLE_ON);
	k_sleep(K_SECONDS(1));

	tcpci_partner_init(&fixture->snk_partner, PD_REV20);
	fixture->snk_partner.extensions = tcpci_snk_emul_init(
		&fixture->snk_ext, &fixture->snk_partner, NULL);
	fixture->snk_partner.cc1 = TYPEC_CC_VOLT_RD;
	fixture->snk_partner.cc2 = TYPEC_CC_VOLT_OPEN;

	tcpci_emul_get_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   &power_reg_val);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   power_reg_val | TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->snk_partner,
						  fixture->tcpci_emul));
	k_sleep(K_SECONDS(10));

	zassert_equal((int)get_state_tc(TEST_PORT), TC_ATTACHED_SRC,
		      "Normal UFP (Rd/Open) must reach TC_ATTACHED_SRC (%d), "
		      "got state %d",
		      TC_ATTACHED_SRC, (int)get_state_tc(TEST_PORT));
}

ZTEST_SUITE(attach_wait_src, drivers_predicate_post_main, attach_wait_src_setup,
	    attach_wait_src_before, attach_wait_src_after, NULL);
