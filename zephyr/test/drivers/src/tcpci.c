/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>
#include <drivers/gpio.h>
#include <drivers/gpio/gpio_emul.h>

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_tcpc.h"
#include "hooks.h"
#include "i2c.h"
#include "stubs.h"

#include "tcpm/tcpci.h"

#define EMUL_LABEL DT_NODELABEL(tcpc_emul)

#define TCPC_ORD DT_DEP_ORD(EMUL_LABEL)

/** Test TCPCI init and vbus level */
static void test_tcpci_init(void)
{
	struct i2c_emul *emul;
	uint16_t exp_mask;

	exp_mask = TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED |
		   TCPC_REG_ALERT_TX_DISCARDED | TCPC_REG_ALERT_RX_STATUS |
		   TCPC_REG_ALERT_RX_HARD_RST | TCPC_REG_ALERT_CC_STATUS |
		   TCPC_REG_ALERT_FAULT | TCPC_REG_ALERT_POWER_STATUS;

	emul = tcpc_emul_get(TCPC_ORD);

	tcpc_config[USBC_PORT_C1].flags = TCPC_FLAGS_TCPCI_REV2_0 &
					  TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V;
	tcpc_emul_set_rev(emul, 2);

	/* Test fail on power status read */
	i2c_common_emul_set_read_fail_reg(emul, TCPC_REG_POWER_STATUS);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpm_init(USBC_PORT_C1), NULL);
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on uninitialised bit set */
	tcpc_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			  TCPC_REG_POWER_STATUS_UNINIT);
	zassert_equal(EC_ERROR_TIMEOUT, tcpci_tcpm_init(USBC_PORT_C1), NULL);

	/* Test init with VBUS safe0v without vsafe0f tcpc config flag */
	tcpc_emul_set_reg(emul, TCPC_REG_POWER_STATUS, 0);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_init(USBC_PORT_C1), NULL);
	zassert_true(tcpci_tcpm_check_vbus_level(USBC_PORT_C1, VBUS_SAFE0V),
		     NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C1, VBUS_PRESENT),
		      NULL);
	zassert_equal(TCPC_REG_POWER_STATUS_VBUS_PRES,
		      tcpc_emul_get_reg(emul, TCPC_REG_POWER_STATUS_MASK),
		      NULL);
	zassert_equal(exp_mask, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT_MASK),
		      NULL);

	/* Test init with VBUS present without vsafe0f tcpc config flag */
	tcpc_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			  TCPC_REG_POWER_STATUS_VBUS_PRES);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_init(USBC_PORT_C1), NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C1, VBUS_SAFE0V),
		      NULL);
	zassert_true(tcpci_tcpm_check_vbus_level(USBC_PORT_C1, VBUS_PRESENT),
		     NULL);
	zassert_equal(TCPC_REG_POWER_STATUS_VBUS_PRES,
		      tcpc_emul_get_reg(emul, TCPC_REG_POWER_STATUS_MASK),
		      NULL);
	zassert_equal(exp_mask, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT_MASK),
		      NULL);

	/* Test init with VBUS present with vsafe0f tcpc config flag */
	exp_mask |= TCPC_REG_ALERT_EXT_STATUS;
	tcpc_config[USBC_PORT_C1].flags = TCPC_FLAGS_TCPCI_REV2_0;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_init(USBC_PORT_C1), NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C1, VBUS_SAFE0V),
		      NULL);
	zassert_true(tcpci_tcpm_check_vbus_level(USBC_PORT_C1, VBUS_PRESENT),
		     NULL);
	zassert_equal(TCPC_REG_POWER_STATUS_VBUS_PRES,
		      tcpc_emul_get_reg(emul, TCPC_REG_POWER_STATUS_MASK),
		      NULL);
	zassert_equal(exp_mask, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT_MASK),
		      NULL);

	/* Test init with VBUS safe0v with vsafe0f tcpc config flag */
	tcpc_emul_set_reg(emul, TCPC_REG_POWER_STATUS, 0);
	tcpc_emul_set_reg(emul, TCPC_REG_EXT_STATUS,
			  TCPC_REG_EXT_STATUS_SAFE0V);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_init(USBC_PORT_C1), NULL);
	zassert_true(tcpci_tcpm_check_vbus_level(USBC_PORT_C1, VBUS_SAFE0V),
		      NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C1, VBUS_PRESENT),
		     NULL);
	zassert_equal(TCPC_REG_POWER_STATUS_VBUS_PRES,
		      tcpc_emul_get_reg(emul, TCPC_REG_POWER_STATUS_MASK),
		      NULL);
	zassert_equal(exp_mask, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT_MASK),
		      NULL);

	/* Test init with VBUS not safe0v with vsafe0f tcpc config flag */
	tcpc_emul_set_reg(emul, TCPC_REG_EXT_STATUS, 0);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_init(USBC_PORT_C1), NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C1, VBUS_SAFE0V),
		      NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C1, VBUS_PRESENT),
		     NULL);
	zassert_equal(TCPC_REG_POWER_STATUS_VBUS_PRES,
		      tcpc_emul_get_reg(emul, TCPC_REG_POWER_STATUS_MASK),
		      NULL);
	zassert_equal(exp_mask, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT_MASK),
		      NULL);
}

/** Test TCPCI release */
static void test_tcpci_release(void)
{
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);
	tcpc_emul_set_reg16(emul, TCPC_REG_ALERT, 0xffff);

	zassert_equal(EC_SUCCESS, tcpci_tcpm_release(USBC_PORT_C1), NULL);
	zassert_equal(0, tcpc_emul_get_reg(emul, TCPC_REG_POWER_STATUS_MASK),
		      NULL);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT_MASK), NULL);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT), NULL);
}

/** Test TCPCI get cc */
static void test_tcpci_get_cc(void)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	/* Set DRP */
	tcpc_emul_set_reg16(emul, TCPC_REG_ROLE_CTRL,
			    TCPC_REG_ROLE_CTRL_DRP_MASK);

	/* Test DRP with open state */
	tcpc_emul_set_reg16(emul, TCPC_REG_CC_STATUS, 0);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_get_cc(USBC_PORT_C1, &cc1, &cc2),
		      NULL);
	zassert_equal(TYPEC_CC_VOLT_OPEN, cc1, NULL);
	zassert_equal(TYPEC_CC_VOLT_OPEN, cc2, NULL);

	/* Test DRP with cc1 open state, cc2 src RA */
	tcpc_emul_set_reg16(emul, TCPC_REG_CC_STATUS, 0x04);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_get_cc(USBC_PORT_C1, &cc1, &cc2),
		      NULL);
	zassert_equal(TYPEC_CC_VOLT_OPEN, cc1, NULL);
	zassert_equal(TYPEC_CC_VOLT_RA, cc2, NULL);

	/* Test DRP with cc1 src RA, cc2 src RD */
	tcpc_emul_set_reg16(emul, TCPC_REG_CC_STATUS, 0x09);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_get_cc(USBC_PORT_C1, &cc1, &cc2),
		      NULL);
	zassert_equal(TYPEC_CC_VOLT_RA, cc1, NULL);
	zassert_equal(TYPEC_CC_VOLT_RD, cc2, NULL);

	/* Test DRP with cc1 snk open, cc2 snk default */
	tcpc_emul_set_reg16(emul, TCPC_REG_CC_STATUS, 0x14);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_get_cc(USBC_PORT_C1, &cc1, &cc2),
		      NULL);
	zassert_equal(TYPEC_CC_VOLT_OPEN, cc1, NULL);
	zassert_equal(TYPEC_CC_VOLT_RP_DEF, cc2, NULL);

	/* Test DRP with cc1 snk 1.5, cc2 snk 3.0 */
	tcpc_emul_set_reg16(emul, TCPC_REG_CC_STATUS, 0x1e);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_get_cc(USBC_PORT_C1, &cc1, &cc2),
		      NULL);
	zassert_equal(TYPEC_CC_VOLT_RP_1_5, cc1, NULL);
	zassert_equal(TYPEC_CC_VOLT_RP_3_0, cc2, NULL);

	/* Test no DRP with cc1 src open, cc2 src RA */
	tcpc_emul_set_reg16(emul, TCPC_REG_ROLE_CTRL, 0x05);
	tcpc_emul_set_reg16(emul, TCPC_REG_CC_STATUS, 0x04);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_get_cc(USBC_PORT_C1, &cc1, &cc2),
		      NULL);
	zassert_equal(TYPEC_CC_VOLT_OPEN, cc1, NULL);
	zassert_equal(TYPEC_CC_VOLT_RA, cc2, NULL);

	/* Test no DRP with cc1 src RD, cc2 snk default */
	tcpc_emul_set_reg16(emul, TCPC_REG_ROLE_CTRL, 0x09);
	tcpc_emul_set_reg16(emul, TCPC_REG_CC_STATUS, 0x06);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_get_cc(USBC_PORT_C1, &cc1, &cc2),
		      NULL);
	zassert_equal(TYPEC_CC_VOLT_RD, cc1, NULL);
	zassert_equal(TYPEC_CC_VOLT_RP_DEF, cc2, NULL);

	/* Test no DRP with cc1 snk default, cc2 snk open */
	tcpc_emul_set_reg16(emul, TCPC_REG_ROLE_CTRL, 0x0a);
	tcpc_emul_set_reg16(emul, TCPC_REG_CC_STATUS, 0x01);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_get_cc(USBC_PORT_C1, &cc1, &cc2),
		      NULL);
	zassert_equal(TYPEC_CC_VOLT_RP_DEF, cc1, NULL);
	zassert_equal(TYPEC_CC_VOLT_OPEN, cc2, NULL);

	/* Test no DRP with cc1 snk 3.0, cc2 snk 1.5 */
	tcpc_emul_set_reg16(emul, TCPC_REG_ROLE_CTRL, 0x0a);
	tcpc_emul_set_reg16(emul, TCPC_REG_CC_STATUS, 0x0b);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_get_cc(USBC_PORT_C1, &cc1, &cc2),
		      NULL);
	zassert_equal(TYPEC_CC_VOLT_RP_3_0, cc1, NULL);
	zassert_equal(TYPEC_CC_VOLT_RP_1_5, cc2, NULL);
}

/** Test TCPCI set cc */
static void test_tcpci_set_cc(void)
{
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	/* Test setting default RP and cc open */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_select_rp_value(USBC_PORT_C1, TYPEC_RP_USB),
		      NULL);
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_cc(USBC_PORT_C1, TYPEC_CC_OPEN), NULL);
	zassert_equal(0x0f, tcpc_emul_get_reg(emul, TCPC_REG_ROLE_CTRL), NULL);

	/* Test error on failed role ctrl set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_ROLE_CTRL);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_set_cc(USBC_PORT_C1, TYPEC_CC_OPEN), NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);


	/* Test setting 1.5 RP and cc RD */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_select_rp_value(USBC_PORT_C1, TYPEC_RP_1A5),
		      NULL);
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_cc(USBC_PORT_C1, TYPEC_CC_RD), NULL);
	zassert_equal(0x1a, tcpc_emul_get_reg(emul, TCPC_REG_ROLE_CTRL), NULL);

	/* Test setting 3.0 RP and cc RP */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_select_rp_value(USBC_PORT_C1, TYPEC_RP_3A0),
		      NULL);
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_cc(USBC_PORT_C1, TYPEC_CC_RP), NULL);
	zassert_equal(0x25, tcpc_emul_get_reg(emul, TCPC_REG_ROLE_CTRL), NULL);

	/* Test setting 3.0 RP and cc RA */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_cc(USBC_PORT_C1, TYPEC_CC_RA), NULL);
	zassert_equal(0x20, tcpc_emul_get_reg(emul, TCPC_REG_ROLE_CTRL), NULL);
}

/** Test TCPCI set polarity */
static void test_tcpci_set_polarity(void)
{
	struct i2c_emul *emul;
	uint8_t exp_ctrl;

	emul = tcpc_emul_get(TCPC_ORD);

	/* Only bit 0 should be changed */
	exp_ctrl = 0x1c;
	tcpc_emul_set_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);

	/* Test error on failed polarity set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_TCPC_CTRL);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_set_polarity(USBC_PORT_C1, POLARITY_CC2),
		      NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_TCPC_CTRL),
		      NULL);

	/* Test setting polarity CC2 */
	exp_ctrl |= 0x1;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_polarity(USBC_PORT_C1,
							  POLARITY_CC2), NULL);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_TCPC_CTRL),
		      NULL);

	/* Test setting polarity CC1 */
	exp_ctrl &= ~0x1;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_polarity(USBC_PORT_C1,
							  POLARITY_CC1), NULL);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_TCPC_CTRL),
		      NULL);

	/* Test setting polarity CC2 DTS */
	exp_ctrl |= 0x1;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_polarity(USBC_PORT_C1,
							  POLARITY_CC2_DTS),
		      NULL);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_TCPC_CTRL),
		      NULL);

	/* Test setting polarity CC1 DTS */
	exp_ctrl &= ~0x1;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_polarity(USBC_PORT_C1,
							  POLARITY_CC1_DTS),
		      NULL);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_TCPC_CTRL),
		      NULL);
}

/** Test TCPCI set vconn */
static void test_tcpci_set_vconn(void)
{
	struct i2c_emul *emul;
	uint8_t exp_ctrl;

	emul = tcpc_emul_get(TCPC_ORD);

	/* Only bit 0 should be changed */
	exp_ctrl = 0x42;
	tcpc_emul_set_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);

	/* Test error on failed vconn set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_POWER_CTRL);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpm_set_vconn(USBC_PORT_C1, 1),
		      NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_POWER_CTRL),
		      NULL);

	/* Test vconn enable */
	exp_ctrl |= 0x1;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_vconn(USBC_PORT_C1, 1), NULL);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_POWER_CTRL),
		      NULL);

	/* Test vconn disable */
	exp_ctrl &= ~0x1;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_vconn(USBC_PORT_C1, 0), NULL);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_POWER_CTRL),
		      NULL);
}

/** Test TCPCI set msg header */
static void test_tcpci_set_msg_header(void)
{
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	/* Test error on failed header set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_MSG_HDR_INFO);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_set_msg_header(USBC_PORT_C1, PD_ROLE_SINK,
						PD_ROLE_UFP), NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test setting sink UFP */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_msg_header(USBC_PORT_C1, PD_ROLE_SINK,
						PD_ROLE_UFP), NULL);
	zassert_equal(0x02, tcpc_emul_get_reg(emul, TCPC_REG_MSG_HDR_INFO),
		      NULL);

	/* Test setting sink DFP */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_msg_header(USBC_PORT_C1, PD_ROLE_SINK,
						PD_ROLE_DFP), NULL);
	zassert_equal(0x0a, tcpc_emul_get_reg(emul, TCPC_REG_MSG_HDR_INFO),
		      NULL);

	/* Test setting source UFP */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_msg_header(USBC_PORT_C1, PD_ROLE_SOURCE,
						PD_ROLE_UFP), NULL);
	zassert_equal(0x03, tcpc_emul_get_reg(emul, TCPC_REG_MSG_HDR_INFO),
		      NULL);

	/* Test setting source DFP */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_msg_header(USBC_PORT_C1, PD_ROLE_SOURCE,
						PD_ROLE_DFP), NULL);
	zassert_equal(0x0b, tcpc_emul_get_reg(emul, TCPC_REG_MSG_HDR_INFO),
		      NULL);
}

/** Test TCPCI rx and sop prime enable */
static void test_tcpci_set_rx_detect(void)
{
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	/* Test error from rx_enable on rx detect set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_RX_DETECT);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpm_set_rx_enable(USBC_PORT_C1, 1),
		      NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test rx disable */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_rx_enable(USBC_PORT_C1, 0),
		      NULL);
	zassert_equal(0x0, tcpc_emul_get_reg(emul, TCPC_REG_RX_DETECT), NULL);

	/* Test setting sop prime with rx disable doesn't change RX_DETECT */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_sop_prime_enable(USBC_PORT_C1, 1),
		      NULL);
	zassert_equal(0x0, tcpc_emul_get_reg(emul, TCPC_REG_RX_DETECT), NULL);

	/* Test that enabling rx after sop prime will set RX_DETECT properly */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_rx_enable(USBC_PORT_C1, 1),
		      NULL);
	zassert_equal(0x27, tcpc_emul_get_reg(emul, TCPC_REG_RX_DETECT), NULL);

	/* Test error from sop_prime on rx detect set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_RX_DETECT);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_sop_prime_enable(USBC_PORT_C1, 0), NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test disabling sop prime with rx enabled does change RX_DETECT */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_sop_prime_enable(USBC_PORT_C1, 0),
		      NULL);
	zassert_equal(0x21, tcpc_emul_get_reg(emul, TCPC_REG_RX_DETECT), NULL);


	/* Test that enabling rx after disabling sop prime set RX_DETECT */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_rx_enable(USBC_PORT_C1, 0),
		      NULL);
	zassert_equal(0x0, tcpc_emul_get_reg(emul, TCPC_REG_RX_DETECT), NULL);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_rx_enable(USBC_PORT_C1, 1),
		      NULL);
	zassert_equal(0x21, tcpc_emul_get_reg(emul, TCPC_REG_RX_DETECT), NULL);
}

/** Test TCPCI get raw message from TCPC */
static void test_tcpci_get_rx_message_raw(void)
{
	struct tcpc_emul_msg msg;
	struct i2c_emul *emul;
	uint32_t payload[7];
	uint8_t buf[32];
	int exp_head;
	int i, head;
	int size;

	emul = tcpc_emul_get(TCPC_ORD);

	tcpc_emul_set_reg16(emul, TCPC_REG_ALERT, 0x0);
	tcpc_emul_set_reg16(emul, TCPC_REG_DEV_CAP_2,
			    TCPC_REG_DEV_CAP_2_LONG_MSG);

	for (i = 0; i < 32; i++) {
		buf[i] = i + 1;
	}
	msg.buf = buf;
	msg.cnt = 32;
	msg.type = TCPCI_MSG_SOP;
	zassert_ok(tcpc_emul_add_rx_msg(emul, &msg, true),
		   "Failed to setup emulator message");

	/* Test fail on reading byte count */
	i2c_common_emul_set_read_fail_reg(emul, TCPC_REG_RX_BUFFER);
	zassert_equal(EC_ERROR_UNKNOWN,
		      tcpci_tcpm_get_message_raw(USBC_PORT_C1, payload, &head),
		      NULL);
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);
	/* Get raw message should always clean RX alerts */
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT), NULL);

	/* Test too short message */
	msg.cnt = 2;
	zassert_ok(tcpc_emul_add_rx_msg(emul, &msg, true),
		   "Failed to setup emulator message");
	zassert_equal(EC_ERROR_UNKNOWN,
		      tcpci_tcpm_get_message_raw(USBC_PORT_C1, payload, &head),
		      NULL);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT), NULL);

	/* Test too long message */
	msg.cnt = 32;
	zassert_ok(tcpc_emul_add_rx_msg(emul, &msg, true),
		   "Failed to setup emulator message");
	zassert_equal(EC_ERROR_UNKNOWN,
		      tcpci_tcpm_get_message_raw(USBC_PORT_C1, payload, &head),
		      NULL);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT), NULL);

	/* Test fail on reading header ??? */

	/* Test alert register and message payload on success */
	size = 28;
	msg.cnt = size + 3;
	msg.type = TCPCI_MSG_SOP_PRIME;
	zassert_ok(tcpc_emul_add_rx_msg(emul, &msg, true),
		   "Failed to setup emulator message");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_get_message_raw(USBC_PORT_C1, payload, &head),
		      NULL);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT), NULL);
	/*
	 * Type is in bits 31-28 of header, buf[0] is in bits 7-0,
	 * buf[1] is in bits 15-8
	 */
	exp_head = (TCPCI_MSG_SOP_PRIME << 28) | (buf[1] << 8) | buf[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf + 2, size, NULL);
}

/** Test TCPCI get raw message from TCPC revision 2.0 */
static void test_tcpci_get_rx_message_raw_rev2(void)
{
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	tcpc_config[USBC_PORT_C1].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpc_emul_set_rev(emul, 2);

	test_tcpci_get_rx_message_raw();
}

/** Test TCPCI get raw message from TCPC revision 1.0 */
static void test_tcpci_get_rx_message_raw_rev1(void)
{
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	tcpc_config[USBC_PORT_C1].flags = 0;
	tcpc_emul_set_rev(emul, 1);

	test_tcpci_get_rx_message_raw();
}

/** Test TCPCI transmitting message from TCPC */
static void test_tcpci_transmit(void)
{
	struct tcpc_emul_msg *msg;
	struct i2c_emul *emul;
	uint32_t data[6];
	uint16_t header;
	int i;

	emul = tcpc_emul_get(TCPC_ORD);
	msg = tcpc_emul_get_tx_msg(emul);

	/* Fill transmit data with pattern */
	for (i = 0; i < 6 * sizeof(uint32_t); i++) {
		((uint8_t *)data)[i] = i;
	}

	/* Test transmit hard reset fail */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_TRANSMIT);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_transmit(USBC_PORT_C1, TCPCI_MSG_TX_HARD_RESET,
					  0, NULL), NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test transmit cabel reset */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_transmit(USBC_PORT_C1, TCPCI_MSG_CABLE_RESET,
					  0, NULL), NULL);
	zassert_equal(TCPCI_MSG_CABLE_RESET, msg->type, NULL);

	/* Test transmit hard reset */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_transmit(USBC_PORT_C1, TCPCI_MSG_TX_HARD_RESET,
					  0, NULL), NULL);
	zassert_equal(TCPCI_MSG_TX_HARD_RESET, msg->type, NULL);

	/* Test transmit fail on rx buffer */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_TX_BUFFER);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_transmit(USBC_PORT_C1, TCPCI_MSG_SOP_PRIME,
					  0, data), NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test transmit only header */
	/* Build random header with count 0 */
	header = PD_HEADER(PD_CTRL_PING, PD_ROLE_SOURCE, PD_ROLE_UFP, 0, 0,
			   PD_REV20, 0);
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_transmit(USBC_PORT_C1, TCPCI_MSG_SOP_PRIME,
					  header, data), NULL);
	zassert_equal(TCPCI_MSG_SOP_PRIME, msg->type, NULL);
	zassert_mem_equal(msg->buf, &header, 2, NULL);
	zassert_equal(2, msg->cnt, NULL);

	/* Test transmit message */
	/* Build random header with count 6 */
	header = PD_HEADER(PD_CTRL_PING, PD_ROLE_SOURCE, PD_ROLE_UFP, 0, 6,
			   PD_REV20, 0);
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_transmit(USBC_PORT_C1, TCPCI_MSG_SOP_PRIME,
					  header, data), NULL);
	zassert_equal(TCPCI_MSG_SOP_PRIME, msg->type, NULL);
	zassert_mem_equal(msg->buf, &header, 2, NULL);
	zassert_mem_equal(msg->buf + 2, data, 6 * sizeof(uint32_t), NULL);
	zassert_equal(2 + 6 * sizeof(uint32_t), msg->cnt, NULL);
}

/** Test TCPCI transmitting message from TCPC revision 2.0 */
static void test_tcpci_transmit_rev2(void)
{
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	tcpc_config[USBC_PORT_C1].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpc_emul_set_rev(emul, 2);

	test_tcpci_transmit();
}

/** Test TCPCI transmitting message from TCPC revision 1.0 */
static void test_tcpci_transmit_rev1(void)
{
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	tcpc_config[USBC_PORT_C1].flags = 0;
	tcpc_emul_set_rev(emul, 1);

	test_tcpci_transmit();
}

/** Test TCPCI alert */
static void test_tcpci_alert(void)
{
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	tcpc_config[USBC_PORT_C1].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpc_emul_set_rev(emul, 2);

	/* Test alert read fail */
	i2c_common_emul_set_read_fail_reg(emul, TCPC_REG_ALERT);
	tcpci_tcpc_alert(USBC_PORT_C1);
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Handle overcurrent */
	tcpc_emul_set_reg16(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_FAULT);
	tcpc_emul_set_reg16(emul, TCPC_REG_FAULT_STATUS,
			    TCPC_REG_FAULT_STATUS_VCONN_OVER_CURRENT);
	tcpci_tcpc_alert(USBC_PORT_C1);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT), NULL);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_FAULT_STATUS),
		      NULL);
	/* Test fault status write (clear fault) */
	/* Test TX complete (maybe with PD test?) */
	tcpc_emul_set_reg16(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_TX_COMPLETE);
	tcpci_tcpc_alert(USBC_PORT_C1);

	/* Test clear alert and ext_alert */
	tcpc_emul_set_reg16(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_ALERT_EXT);
	tcpc_emul_set_reg16(emul, TCPC_REG_ALERT_EXT,
			    TCPC_REG_ALERT_EXT_TIMER_EXPIRED);
	tcpci_tcpc_alert(USBC_PORT_C1);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT), NULL);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT_EXT), NULL);
	/* Test CC changed */
	tcpc_emul_set_reg16(emul, TCPC_REG_CC_STATUS, 0x1e);
	tcpc_emul_set_reg16(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_CC_STATUS);
	tcpci_tcpc_alert(USBC_PORT_C1);
	/* Test Hard reset */
	tcpc_emul_set_reg16(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_RX_HARD_RST);
	tcpci_tcpc_alert(USBC_PORT_C1);
}


/** Test TCPCI alert RX message */
static void test_tcpci_alert_rx_message(void)
{
	struct tcpc_emul_msg msg1, msg2;
	uint8_t buf1[32], buf2[32];
	struct i2c_emul *emul;
	uint32_t payload[7];
	int exp_head;
	int i, head;
	int size;

	emul = tcpc_emul_get(TCPC_ORD);

	tcpc_config[USBC_PORT_C1].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpc_emul_set_rev(emul, 2);

	for (i = 0; i < 32; i++) {
		buf1[i] = i + 1;
		buf2[i] = i + 33;
	}
	size = 23;
	msg1.buf = buf1;
	msg1.cnt = size + 3;
	msg1.type = TCPCI_MSG_SOP;

	msg2.buf = buf2;
	msg2.cnt = size + 3;
	msg2.type = TCPCI_MSG_SOP_PRIME;

	/* Test receiving one message */
	zassert_ok(tcpc_emul_add_rx_msg(emul, &msg1, true),
		   "Failed to setup emulator message");
	tcpci_tcpc_alert(USBC_PORT_C1);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT), NULL);

	/* Check if msg1 is in queue */
	zassert_true(tcpm_has_pending_message(USBC_PORT_C1), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(USBC_PORT_C1, payload,
						       &head), NULL);
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf1 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(USBC_PORT_C1), NULL);

	/* Test receiving two messages */
	zassert_ok(tcpc_emul_add_rx_msg(emul, &msg1, true),
		   "Failed to setup emulator message");
	zassert_ok(tcpc_emul_add_rx_msg(emul, &msg2, true),
		   "Failed to setup emulator message");
	tcpci_tcpc_alert(USBC_PORT_C1);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT), NULL);

	/* Check if msg1 is in queue */
	zassert_true(tcpm_has_pending_message(USBC_PORT_C1), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(USBC_PORT_C1, payload,
						       &head), NULL);
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf1 + 2, size, NULL);
	/* Check if msg2 is in queue */
	zassert_true(tcpm_has_pending_message(USBC_PORT_C1), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(USBC_PORT_C1, payload,
						       &head), NULL);
	exp_head = (TCPCI_MSG_SOP_PRIME << 28) | (buf2[1] << 8) | buf2[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf2 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(USBC_PORT_C1), NULL);

	/* Test with too long first message */
	msg1.cnt = 32;
	zassert_ok(tcpc_emul_add_rx_msg(emul, &msg1, true),
		   "Failed to setup emulator message");
	zassert_ok(tcpc_emul_add_rx_msg(emul, &msg2, true),
		   "Failed to setup emulator message");
	tcpci_tcpc_alert(USBC_PORT_C1);
	zassert_equal(0, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT), NULL);

	/* Check if msg2 is in queue */
	zassert_true(tcpm_has_pending_message(USBC_PORT_C1), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(USBC_PORT_C1, payload,
						       &head), NULL);
	exp_head = (TCPCI_MSG_SOP_PRIME << 28) | (buf2[1] << 8) | buf2[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf2 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(USBC_PORT_C1), NULL);

	/* Test constant read message failure */
	zassert_ok(tcpc_emul_add_rx_msg(emul, &msg1, true),
		   "Failed to setup emulator message");
	/* Create loop with one message with wrong size */
	msg1.next = &msg1;
	tcpci_tcpc_alert(USBC_PORT_C1);
	/* Nothing should be in queue */
	zassert_false(tcpm_has_pending_message(USBC_PORT_C1), NULL);

	/* Test constant correct messages stream */
	msg1.cnt = size + 3;
	tcpci_tcpc_alert(USBC_PORT_C1);
	msg1.next = NULL;

	/* msg1 should be at least twice in queue */
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	for (i = 0; i < 2; i++) {
		zassert_true(tcpm_has_pending_message(USBC_PORT_C1), NULL);
		zassert_equal(EC_SUCCESS,
			      tcpm_dequeue_message(USBC_PORT_C1, payload,
						   &head), NULL);
		zassert_equal(exp_head, head,
			      "Received header 0x%08lx, expected 0x%08lx",
			      head, exp_head);
		zassert_mem_equal(payload, buf1 + 2, size, NULL);
	}
	tcpm_clear_pending_messages(USBC_PORT_C1);
	zassert_false(tcpm_has_pending_message(USBC_PORT_C1), NULL);
}

/** Test TCPCI auto discharge on disconnect */
static void test_tcpci_auto_discharge(void)
{
	struct i2c_emul *emul;
	uint8_t exp_ctrl;

	emul = tcpc_emul_get(TCPC_ORD);

	/* Only bit 4 should be changed */
	exp_ctrl = 0x42;
	tcpc_emul_set_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);

	/* Test discharge enable */
	exp_ctrl |= 0x10;
	tcpci_tcpc_enable_auto_discharge_disconnect(USBC_PORT_C1, 1);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_POWER_CTRL),
		      NULL);

	/* Test discharge disable */
	exp_ctrl &= ~0x10;
	tcpci_tcpc_enable_auto_discharge_disconnect(USBC_PORT_C1, 0);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_POWER_CTRL),
		      NULL);
}

/** Test TCPCI drp toggle */
static void test_tcpci_drp_toggle(void)
{
	uint8_t exp_tcpc_ctrl, exp_role_ctrl;
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	tcpc_config[USBC_PORT_C1].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpc_emul_set_rev(emul, 2);

	/* Test error on failed role CTRL set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_ROLE_CTRL);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpc_drp_toggle(USBC_PORT_C1),
		      NULL);

	/* Test error on failed TCPC CTRL set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_TCPC_CTRL);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpc_drp_toggle(USBC_PORT_C1),
		      NULL);

	/* Test error on failed command set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_COMMAND);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpc_drp_toggle(USBC_PORT_C1),
		      NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test correct registers values for rev 2.0 */
	exp_role_ctrl = 0x45;
	/* Only look for connection bit (6) should be changed  */
	exp_tcpc_ctrl = 0x9;
	tcpc_emul_set_reg(emul, TCPC_REG_TCPC_CTRL, exp_tcpc_ctrl);
	exp_tcpc_ctrl |= 0x40;
	zassert_equal(EC_SUCCESS, tcpci_tcpc_drp_toggle(USBC_PORT_C1), NULL);
	zassert_equal(exp_tcpc_ctrl,
		      tcpc_emul_get_reg(emul, TCPC_REG_TCPC_CTRL), NULL);
	zassert_equal(exp_role_ctrl,
		      tcpc_emul_get_reg(emul, TCPC_REG_ROLE_CTRL), NULL);
	zassert_equal(TCPC_REG_COMMAND_LOOK4CONNECTION,
		      tcpc_emul_get_reg(emul, TCPC_REG_COMMAND), NULL);

	/* Test correct registers values for rev 1.0 */
	tcpc_config[USBC_PORT_C1].flags = 0;
	tcpc_emul_set_rev(emul, 1);
	exp_role_ctrl = 0x4a;
	/* Only look for connection bit (6) should be changed  */
	exp_tcpc_ctrl = 0x9;
	tcpc_emul_set_reg(emul, TCPC_REG_TCPC_CTRL, exp_tcpc_ctrl);
	exp_tcpc_ctrl |= 0x40;
	zassert_equal(EC_SUCCESS, tcpci_tcpc_drp_toggle(USBC_PORT_C1), NULL);
	zassert_equal(exp_tcpc_ctrl,
		      tcpc_emul_get_reg(emul, TCPC_REG_TCPC_CTRL), NULL);
	zassert_equal(exp_role_ctrl,
		      tcpc_emul_get_reg(emul, TCPC_REG_ROLE_CTRL), NULL);
	zassert_equal(TCPC_REG_COMMAND_LOOK4CONNECTION,
		      tcpc_emul_get_reg(emul, TCPC_REG_COMMAND), NULL);
}

/** Test TCPCI get chip info */
static void test_tcpci_get_chip_info(void)
{
	struct ec_response_pd_chip_info_v1 info;
	uint16_t vendor, product, bcd;
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	/* Test error on failed vendor id get */
	i2c_common_emul_set_read_fail_reg(emul, TCPC_REG_VENDOR_ID);
	zassert_equal(EC_ERROR_INVAL, tcpci_get_chip_info(USBC_PORT_C1, 1,
							  &info), NULL);

	/* Test error on failed product id get */
	i2c_common_emul_set_read_fail_reg(emul, TCPC_REG_PRODUCT_ID);
	zassert_equal(EC_ERROR_INVAL, tcpci_get_chip_info(USBC_PORT_C1, 1,
							  &info), NULL);

	/* Test error on failed BCD get */
	i2c_common_emul_set_read_fail_reg(emul, TCPC_REG_VENDOR_ID);
	zassert_equal(EC_ERROR_INVAL, tcpci_get_chip_info(USBC_PORT_C1, 1,
							  &info), NULL);
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test reading chip info */
	vendor = 0x1234;
	product = 0x5678;
	bcd = 0x9876;
	tcpc_emul_set_reg16(emul, TCPC_REG_VENDOR_ID, vendor);
	tcpc_emul_set_reg16(emul, TCPC_REG_PRODUCT_ID, product);
	tcpc_emul_set_reg16(emul, TCPC_REG_BCD_DEV, bcd);
	zassert_equal(EC_SUCCESS, tcpci_get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);
	zassert_equal(vendor, info.vendor_id, NULL);
	zassert_equal(product, info.product_id, NULL);
	zassert_equal(bcd, info.device_id, NULL);

	/* Test reading cached chip info */
	info.vendor_id = 0;
	info.product_id = 0;
	info.device_id = 0;
	/* Make sure, that TCPC is not accessed */
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_SUCCESS, tcpci_get_chip_info(USBC_PORT_C1, 0, &info),
		      NULL);
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_equal(vendor, info.vendor_id, NULL);
	zassert_equal(product, info.product_id, NULL);
	zassert_equal(bcd, info.device_id, NULL);
}

/** Test TCPCI enter low power mode */
static void test_tcpci_low_power_mode(void)
{
	struct i2c_emul *emul;

	emul = tcpc_emul_get(TCPC_ORD);

	/* Test error on failed command set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_COMMAND);
	zassert_equal(EC_ERROR_INVAL, tcpci_enter_low_power_mode(USBC_PORT_C1),
		      NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test correct command is issued */
	zassert_equal(EC_SUCCESS, tcpci_enter_low_power_mode(USBC_PORT_C1),
		      NULL);
	zassert_equal(TCPC_REG_COMMAND_I2CIDLE,
		      tcpc_emul_get_reg(emul, TCPC_REG_COMMAND), NULL);
}

/** Test TCPCI set bist test mode */
static void test_tcpci_set_bist_mode(void)
{
	struct i2c_emul *emul;
	uint16_t exp_mask;
	uint8_t exp_ctrl;

	emul = tcpc_emul_get(TCPC_ORD);

	/* Test error on TCPC CTRL set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_TCPC_CTRL);
	zassert_equal(EC_ERROR_INVAL, tcpci_set_bist_test_mode(USBC_PORT_C1, 1),
		      NULL);

	/* Test error on alert mask set */
	i2c_common_emul_set_write_fail_reg(emul, TCPC_REG_ALERT_MASK);
	zassert_equal(EC_ERROR_INVAL, tcpci_set_bist_test_mode(USBC_PORT_C1, 1),
		      NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test enabling bist test mode */
	exp_mask = 0x7fff;
	tcpc_emul_set_reg16(emul, TCPC_REG_ALERT_MASK, exp_mask);
	/* RX status alert should be disabled */
	exp_mask &= ~0x4;
	exp_ctrl = 0x45;
	tcpc_emul_set_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);
	/* BIST test mode should be enabled */
	exp_ctrl |= 0x2;
	zassert_equal(EC_SUCCESS, tcpci_set_bist_test_mode(USBC_PORT_C1, 1),
		      NULL);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_TCPC_CTRL),
		      NULL);
	zassert_equal(exp_mask, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT_MASK),
		      NULL);

	/*
	 * Test disabling bist test mode. RX status alert should be enabled and
	 * BIST test mode should be disabled.
	 */
	exp_mask |= 0x4;
	exp_ctrl &= ~0x2;
	zassert_equal(EC_SUCCESS, tcpci_set_bist_test_mode(USBC_PORT_C1, 0),
		      NULL);
	zassert_equal(exp_ctrl, tcpc_emul_get_reg(emul, TCPC_REG_TCPC_CTRL),
		      NULL);
	zassert_equal(exp_mask, tcpc_emul_get_reg16(emul, TCPC_REG_ALERT_MASK),
		      NULL);
}

void test_suite_tcpci(void)
{
	struct tcpc_config_t tcpc_config_temp;

	/* Copy original tcpc configuration */
	memcpy(&tcpc_config_temp, &tcpc_config[USBC_PORT_C1],
	       sizeof(struct tcpc_config_t));

	tcpc_config[USBC_PORT_C1].bus_type = EC_BUS_TYPE_I2C;
	tcpc_config[USBC_PORT_C1].i2c_info.port = I2C_PORT_USB_C1;
	tcpc_config[USBC_PORT_C1].i2c_info.addr_flags = DT_REG_ADDR(EMUL_LABEL);
	tcpc_config[USBC_PORT_C1].drv = &tcpci_tcpm_drv;

	ztest_test_suite(tcpci,
			 ztest_user_unit_test(test_tcpci_init),
			 ztest_user_unit_test(test_tcpci_release),
			 ztest_user_unit_test(test_tcpci_get_cc),
			 ztest_user_unit_test(test_tcpci_set_cc),
			 ztest_user_unit_test(test_tcpci_set_polarity),
			 ztest_user_unit_test(test_tcpci_set_vconn),
			 ztest_user_unit_test(test_tcpci_set_msg_header),
			 ztest_user_unit_test(test_tcpci_set_rx_detect),
			 ztest_user_unit_test(
				test_tcpci_get_rx_message_raw_rev2),
			 ztest_user_unit_test(test_tcpci_transmit_rev2),
			 ztest_user_unit_test(
				test_tcpci_get_rx_message_raw_rev1),
			 ztest_user_unit_test(test_tcpci_transmit_rev1),
			 ztest_user_unit_test(test_tcpci_alert),
			 ztest_user_unit_test(test_tcpci_alert_rx_message),
			 ztest_user_unit_test(test_tcpci_auto_discharge),
			 ztest_user_unit_test(test_tcpci_drp_toggle),
			 ztest_user_unit_test(test_tcpci_get_chip_info),
			 ztest_user_unit_test(test_tcpci_low_power_mode),
			 ztest_user_unit_test(test_tcpci_set_bist_mode));
	ztest_run_test_suite(tcpci);

	/* Restore original tcpc configuration */
	memcpy(&tcpc_config[USBC_PORT_C1], &tcpc_config_temp,
	       sizeof(struct tcpc_config_t));
}
