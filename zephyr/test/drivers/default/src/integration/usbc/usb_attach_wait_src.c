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
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0
BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

static atomic_t test_port_is_dts = ATOMIC_INIT(1);

__override bool board_is_dts_port(int port)
{
	return atomic_get(&test_port_is_dts) != 0;
}

struct attach_wait_src_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data dbg_acc;
	struct tcpci_snk_emul_data dbg_snk_ext;
	struct tcpci_partner_data ufp;
	struct tcpci_snk_emul_data ufp_snk_ext;
};

static struct tcpci_partner_extension_ops dbg_acc_snk_ops;
static int (*original_snk_connect)(struct tcpci_partner_extension *ext,
				   struct tcpci_partner_data *common_data);

static int dbg_acc_connect(struct tcpci_partner_extension *ext,
			   struct tcpci_partner_data *common_data)
{
	int ret = 0;

	if (original_snk_connect)
		ret = original_snk_connect(ext, common_data);

	/* Maintain Rd on both CC1 and CC2 for Debug Accessory */
	common_data->cc1 = TYPEC_CC_VOLT_RD;
	common_data->cc2 = TYPEC_CC_VOLT_RD;
	common_data->polarity = POLARITY_CC1;

	return ret;
}

static void *attach_wait_src_setup(void)
{
	static struct attach_wait_src_fixture fixture;

	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	/* Initialize Debug Accessory Partner (Rd / Rd) with sink extension */
	tcpci_partner_init(&fixture.dbg_acc, PD_REV20);
	fixture.dbg_acc.extensions = tcpci_snk_emul_init(
		&fixture.dbg_snk_ext, &fixture.dbg_acc, NULL);
	dbg_acc_snk_ops = *fixture.dbg_snk_ext.ext.ops;
	original_snk_connect = dbg_acc_snk_ops.connect;
	dbg_acc_snk_ops.connect = dbg_acc_connect;
	fixture.dbg_snk_ext.ext.ops = &dbg_acc_snk_ops;
	fixture.dbg_acc.power_role = PD_ROLE_SINK;
	fixture.dbg_acc.cc1 = TYPEC_CC_VOLT_RD;
	fixture.dbg_acc.cc2 = TYPEC_CC_VOLT_RD;
	fixture.dbg_acc.polarity = POLARITY_CC1;

	/* Initialize Normal UFP Partner (Rd / Open) with sink extension */
	tcpci_partner_init(&fixture.ufp, PD_REV20);
	fixture.ufp.extensions =
		tcpci_snk_emul_init(&fixture.ufp_snk_ext, &fixture.ufp, NULL);
	fixture.ufp.power_role = PD_ROLE_SINK;
	fixture.ufp.cc1 = TYPEC_CC_VOLT_RD;
	fixture.ufp.cc2 = TYPEC_CC_VOLT_OPEN;
	fixture.ufp.polarity = POLARITY_CC1;

	/* Ensure port is unsuspended if a preceding suite left it suspended */
	pd_set_suspend(TEST_PORT, false);

	return &fixture;
}

static void attach_wait_src_before(void *state)
{
	struct attach_wait_src_fixture *fixture = state;

	atomic_set(&test_port_is_dts, 1);
	pd_set_dual_role(TEST_PORT, PD_DRP_TOGGLE_ON);
	disconnect_sink_from_port(fixture->tcpci_emul);
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	test_set_chipset_to_s0();
}

static void attach_wait_src_after(void *state)
{
	struct attach_wait_src_fixture *fixture = state;

	atomic_set(&test_port_is_dts, 1);
	pd_set_dual_role(TEST_PORT, PD_DRP_TOGGLE_ON);
	disconnect_sink_from_port(fixture->tcpci_emul);
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
}

static void connect_partner(struct attach_wait_src_fixture *fixture,
			    struct tcpci_partner_data *partner)
{
	uint16_t power_reg_val;

	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_SAFE0V));

	tcpci_emul_get_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   &power_reg_val);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   power_reg_val | TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	zassert_ok(
		tcpci_partner_connect_to_tcpci(partner, fixture->tcpci_emul));

	/* Wait for CC debounce and state machine transitions to settle */
	k_sleep(K_SECONDS(2));
}

/* -------------------------------------------------------------------------
 * Test cases
 * ------------------------------------------------------------------------- */

/**
 * @brief Verify Debug Accessory (Rd/Rd) attaches as Source on DTS port.
 */
ZTEST_F(attach_wait_src, test_dbg_acc_dts_port)
{
	struct ec_response_typec_status status;
	struct ec_response_usb_pd_power_info info;
	uint16_t role_ctrl;

	connect_partner(fixture, &fixture->dbg_acc);

	/*
	 * Verify Type-C status reports connected debug accessory
	 * in Source role.
	 */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_true(status.dev_connected, "Device expected to be connected");
	zassert_equal(status.power_role, PD_ROLE_SOURCE,
		      "Expected power role SOURCE, got %d", status.power_role);
	zassert_equal(strcmp(status.tc_state, "Attached.SRC"), 0,
		      "Expected TC state 'Attached.SRC', got '%s'",
		      status.tc_state);

	/* Verify VBUS is enabled and sourcing ~5V */
	info = host_cmd_power_info(TEST_PORT);
	zassert_equal(info.role, USB_PD_PORT_POWER_SOURCE,
		      "Expected power role SOURCE in power_info, got %d",
		      info.role);
	zassert_within(info.meas.voltage_now, 5000, 500,
		       "Expected VBUS near 5000mV, got %dmV",
		       info.meas.voltage_now);

	/* Verify TCPC is presenting Rp */
	zassert_ok(tcpci_emul_get_reg(fixture->tcpci_emul, TCPC_REG_ROLE_CTRL,
				      &role_ctrl));
	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(role_ctrl), TYPEC_CC_RP,
		      "Expected TCPC CC1 to present Rp, got %d",
		      TCPC_REG_ROLE_CTRL_CC1(role_ctrl));
}

/**
 * @brief Verify Debug Accessory (Rd/Rd) on non-DTS port exits AttachWait.SRC
 *        to Sink role upon FORCE_SINK DRP policy change.
 */
ZTEST_F(attach_wait_src, test_dbg_acc_no_dts_force_sink)
{
	struct ec_response_typec_status status;
	struct ec_response_usb_pd_power_info info;
	uint16_t role_ctrl;

	/* Connect Debug Accessory on non-DTS port in AttachWait.SRC */
	atomic_set(&test_port_is_dts, 0);
	pd_set_dual_role(TEST_PORT, PD_DRP_TOGGLE_ON);
	connect_partner(fixture, &fixture->dbg_acc);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal(strcmp(status.tc_state, "AttachWait.SRC"), 0,
		      "Expected TC state 'AttachWait.SRC', got '%s'",
		      status.tc_state);

	/* Switch DRP policy to FORCE_SINK */
	pd_set_dual_role(TEST_PORT, PD_DRP_FORCE_SINK);
	k_sleep(K_SECONDS(1));

	/*
	 * Verify port transitioned to Sink role and did not attach
	 * as Source.
	 */
	status = host_cmd_typec_status(TEST_PORT);
	info = host_cmd_power_info(TEST_PORT);
	zassert_not_equal(status.power_role, PD_ROLE_SOURCE,
			  "Port must NOT have Source power role");
	zassert_not_equal(info.role, USB_PD_PORT_POWER_SOURCE,
			  "VBUS must NOT be sourced");

	/* Verify TCPC role control presents Rd */
	zassert_ok(tcpci_emul_get_reg(fixture->tcpci_emul, TCPC_REG_ROLE_CTRL,
				      &role_ctrl));
	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(role_ctrl), TYPEC_CC_RD,
		      "Expected TCPC CC1 to present Rd in sink mode, got %d",
		      TCPC_REG_ROLE_CTRL_CC1(role_ctrl));
}

/**
 * @brief Verify Debug Accessory (Rd/Rd) on non-DTS port remains unattached
 *        in AttachWait.SRC indefinitely per Type-C r2.4 §4.5.2.2.8.2 /
 *        TD 4.8.2.
 */
ZTEST_F(attach_wait_src, test_dbg_acc_no_dts_toggle_on)
{
	struct ec_response_typec_status status;
	struct ec_response_usb_pd_power_info info;

	atomic_set(&test_port_is_dts, 0);
	pd_set_dual_role(TEST_PORT, PD_DRP_TOGGLE_ON);
	k_sleep(K_MSEC(100));

	connect_partner(fixture, &fixture->dbg_acc);

	/* Verify port did not attach and is not recognized as normal UFP */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_false(status.dev_connected,
		      "Port must NOT attach for Rd/Rd on non-DTS port");
	zassert_not_equal(status.cc_state, PD_CC_UFP_ATTACHED,
			  "Rd/Rd must NOT be misidentified as normal UFP");
	zassert_equal(strcmp(status.tc_state, "AttachWait.SRC"), 0,
		      "Expected TC state 'AttachWait.SRC', got '%s'",
		      status.tc_state);

	/* Verify VBUS remains unpowered (0V) */
	info = host_cmd_power_info(TEST_PORT);
	zassert_not_equal(info.role, USB_PD_PORT_POWER_SOURCE,
			  "VBUS must NOT be enabled in AttachWait.SRC");
	zassert_equal(info.meas.voltage_now, 0, "VBUS must be 0mV, got %dmV",
		      info.meas.voltage_now);
}

/**
 * @brief Verify Normal UFP (Rd/Open) attaches as Source.
 */
ZTEST_F(attach_wait_src, test_ufp_attached)
{
	struct ec_response_typec_status status;
	struct ec_response_usb_pd_power_info info;
	uint16_t role_ctrl;

	connect_partner(fixture, &fixture->ufp);

	/* Verify Type-C status reports normal UFP connected as Source */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_true(status.dev_connected, "Expected UFP to be connected");
	zassert_equal(status.power_role, PD_ROLE_SOURCE,
		      "Expected power role SOURCE, got %d", status.power_role);
	zassert_equal(strcmp(status.tc_state, "Attached.SRC"), 0,
		      "Expected TC state 'Attached.SRC', got '%s'",
		      status.tc_state);

	/* Verify VBUS is enabled and sourcing ~5V */
	info = host_cmd_power_info(TEST_PORT);
	zassert_equal(info.role, USB_PD_PORT_POWER_SOURCE,
		      "Expected power role SOURCE in power_info, got %d",
		      info.role);
	zassert_within(info.meas.voltage_now, 5000, 500,
		       "Expected VBUS near 5000mV, got %dmV",
		       info.meas.voltage_now);

	/* Verify TCPC is presenting Rp */
	zassert_ok(tcpci_emul_get_reg(fixture->tcpci_emul, TCPC_REG_ROLE_CTRL,
				      &role_ctrl));
	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(role_ctrl), TYPEC_CC_RP,
		      "Expected TCPC CC1 to present Rp, got %d",
		      TCPC_REG_ROLE_CTRL_CC1(role_ctrl));
}

ZTEST_SUITE(attach_wait_src, drivers_predicate_post_main, attach_wait_src_setup,
	    attach_wait_src_before, attach_wait_src_after, NULL);
