/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "driver/tcpm/raa489000.h"
#include "emul/tcpc/emul_tcpci.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "usb_charge.h"
#include "usb_pd.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <dt-bindings/gpio_defines.h>
#include <typec_control.h>

#define TCPC0 EMUL_DT_GET(DT_NODELABEL(tcpc_port0))
#define TCPC1 EMUL_DT_GET(DT_NODELABEL(tcpc_port1))

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

void usb_interrupt(enum gpio_signal signal);
enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value);
enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value);

FAKE_VALUE_FUNC(int, raa489000_enable_asgate, int, bool);
FAKE_VALUE_FUNC(int, charge_manager_get_active_charge_port);
FAKE_VALUE_FUNC(enum ec_error_list, charger_discharge_on_ac, int);
FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VOID_FUNC(usb_charger_task_set_event_sync, int, uint8_t);
FAKE_VOID_FUNC(usb_interrupt_c1, enum gpio_signal);
FAKE_VALUE_FUNC(uint8_t, board_get_usb_pd_port_count);

static uint16_t vbus_current_target[2];

static int tcpc_write_func(const struct emul *target, int reg, uint8_t val,
			   int bytes, void *data)
{
	int port = (target == TCPC0) ? 0 : 1;

	if (reg == RAA489000_VBUS_CURRENT_TARGET) {
		if (bytes == 1)
			vbus_current_target[port] = val;
		else if (bytes == 2)
			vbus_current_target[port] |= ((uint16_t)val << 8);
		return 0;
	}
	return 1;
}

static void test_before(void *fixture)
{
	const struct emul *tcpcs[] = { TCPC0, TCPC1 };

	RESET_FAKE(raa489000_enable_asgate);
	RESET_FAKE(charge_manager_get_active_charge_port);
	RESET_FAKE(charger_discharge_on_ac);
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(usb_charger_task_set_event_sync);
	RESET_FAKE(usb_interrupt_c1);
	RESET_FAKE(board_get_usb_pd_port_count);

	board_get_usb_pd_port_count_fake.return_val = 2;
	for (int i = 0; i < 2; i++) {
		struct i2c_common_emul_data *i2c =
			emul_tcpci_generic_get_i2c_common_data(tcpcs[i]);

		vbus_current_target[i] = 0;
		tcpci_emul_set_reg(tcpcs[i], TCPC_REG_POWER_STATUS, 0);
		tcpci_emul_set_reg(tcpcs[i], TCPC_REG_COMMAND, 0);
		tcpci_emul_set_reg(tcpcs[i], TCPC_REG_ALERT, 0);
		i2c_common_emul_set_write_func(i2c, tcpc_write_func, NULL);
		i2c_common_emul_set_write_fail_reg(i2c,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
	}
}

ZTEST_SUITE(xivu, NULL, NULL, test_before, NULL, NULL);

ZTEST(xivu, test_1_port_sku_usbc)
{
	uint16_t reg;

	/* Simulate 1-port sub-board SKU (e.g. NISSA_SB_HDMI_A / NISSA_SB_NONE)
	 */
	board_get_usb_pd_port_count_fake.return_val = 1;

	zassert_equal(board_set_active_charge_port(1), EC_ERROR_INVAL);
	zassert_equal(pd_set_power_supply_ready(1), EC_ERROR_INVAL);

	/* Setting CHARGE_PORT_NONE or Port 0 must never touch Port 1 */
	tcpci_emul_set_reg(TCPC1, TCPC_REG_COMMAND, 0xff);
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(raa489000_enable_asgate_fake.call_count, 1);
	zassert_equal(raa489000_enable_asgate_fake.arg0_history[0], 0);
	zassert_false(raa489000_enable_asgate_fake.arg1_history[0]);
	tcpci_emul_get_reg(TCPC1, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, 0xff);

	RESET_FAKE(raa489000_enable_asgate);
	zassert_ok(board_set_active_charge_port(0));
	zassert_equal(raa489000_enable_asgate_fake.call_count, 1);
	zassert_equal(raa489000_enable_asgate_fake.arg0_history[0], 0);
	zassert_true(raa489000_enable_asgate_fake.arg1_history[0]);
	tcpci_emul_get_reg(TCPC1, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, 0xff);

	vbus_current_target[1] = 0x1234;
	typec_set_source_current_limit(1, TYPEC_RP_3A0);
	zassert_equal(vbus_current_target[1], 0x1234);

	typec_set_source_current_limit(0, TYPEC_RP_3A0);
	zassert_equal(vbus_current_target[0], 0x68);
}

ZTEST(xivu, test_set_active_charge_port_2_ports)
{
	uint16_t reg;

	tcpci_emul_set_reg(TCPC0, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_SOURCING_VBUS);
	zassert_true(board_is_sourcing_vbus(0));
	zassert_false(board_is_sourcing_vbus(1));
	tcpci_emul_set_reg(TCPC0, TCPC_REG_POWER_STATUS, 0);

	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(raa489000_enable_asgate_fake.call_count, 2);
	tcpci_emul_get_reg(TCPC0, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SNK_CTRL_LOW);
	tcpci_emul_get_reg(TCPC1, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SNK_CTRL_LOW);

	zassert_equal(board_set_active_charge_port(4), EC_ERROR_INVAL);
	tcpci_emul_set_reg(TCPC1, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_SOURCING_VBUS);
	zassert_equal(board_set_active_charge_port(1), EC_ERROR_INVAL);

	RESET_FAKE(raa489000_enable_asgate);
	zassert_ok(board_set_active_charge_port(0));
	zassert_equal(raa489000_enable_asgate_fake.call_count, 2);
	zassert_equal(charger_discharge_on_ac_fake.call_count, 2);
	tcpci_emul_get_reg(TCPC0, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SNK_CTRL_HIGH);

	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC1),
		TCPC_REG_COMMAND);
	zassert_ok(board_set_active_charge_port(0));

	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		TCPC_REG_COMMAND);
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_UNKNOWN);
}

static int chipset_in_state_break_tcpc_command(int state_mask)
{
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		TCPC_REG_COMMAND);
	return 0;
}

ZTEST(xivu, test_pd_power_supply_and_alerts)
{
	const struct gpio_dt_spec *c0_int =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl);
	const struct gpio_dt_spec *c1_int =
		GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl);
	uint16_t reg;
	uint32_t val;

	gpio_pin_configure_dt(c1_int, GPIO_INPUT_PULL_UP);
	gpio_emul_input_set(c0_int->port, c0_int->pin, 0);
	gpio_emul_input_set(c1_int->port, c1_int->pin, 0);
	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT, 1);
	zassert_equal(tcpc_get_alert_status(), PD_STATUS_TCPC_ALERT_0);
	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT, 0x4000);
	zassert_equal(tcpc_get_alert_status(), 0);
	tcpci_emul_set_reg(TCPC1, TCPC_REG_ALERT, 0x8000);
	zassert_equal(tcpc_get_alert_status(), PD_STATUS_TCPC_ALERT_1);
	board_get_usb_pd_port_count_fake.return_val = 1;
	zassert_equal(tcpc_get_alert_status(), 0);
	board_get_usb_pd_port_count_fake.return_val = 2;

	pd_power_supply_reset(0);
	tcpci_emul_get_reg(TCPC0, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SRC_CTRL_LOW);

	typec_set_source_current_limit(0, TYPEC_RP_1A5);
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		RAA489000_VBUS_CURRENT_TARGET);
	typec_set_source_current_limit(0, TYPEC_RP_3A0);
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		I2C_COMMON_EMUL_NO_FAIL_REG);

	zassert_ok(pd_set_power_supply_ready(0));
	raa489000_enable_asgate_fake.return_val = EC_ERROR_UNIMPLEMENTED;
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
	raa489000_enable_asgate_fake.return_val = EC_SUCCESS;

	chipset_in_state_fake.custom_fake = chipset_in_state_break_tcpc_command;
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
	chipset_in_state_fake.custom_fake = NULL;

	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		TCPC_REG_COMMAND);
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		I2C_COMMON_EMUL_NO_FAIL_REG);

	chipset_in_state_fake.return_val = 1;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_NOT_POWERED);

	board_reset_pd_mcu();
	board_process_pd_alert(0);
	board_process_pd_alert(1);
	zassert_equal(usb_charger_task_set_event_sync_fake.call_count, 2);

	usb_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c0_int_odl)));
	usb_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_sb_1)));
	zassert_equal(charger_profile_override_get_param(0, &val),
		      EC_RES_INVALID_PARAM);
	zassert_equal(charger_profile_override_set_param(0, 0),
		      EC_RES_INVALID_PARAM);
}
