/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "baseboard_usbc_config.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/rt1718s.h"
#include "driver/tcpm/tcpm.h"
#include "driver/usb_mux/ps8743_public.h"
#include "ec_app_main.h"
#include "emul/tcpc/emul_rt1718s.h"
#include "emul/tcpc/emul_tcpci.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "i2c.h"
#include "test/drivers/utils.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "variant_db_detection.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define LOG_LEVEL 0
LOG_MODULE_REGISTER(npcx_usbc);

extern void board_tcpc_init(void);
extern int ps8743_mux_1_board_init(const struct usb_mux *me);
extern void board_usb_mux_init(void);
extern int board_rt1718s_set_frs_enable(int port, int enable);
extern int board_rt1718s_set_snk_enable(int port, int enable);
extern int board_rt1718s_set_src_enable(int port, int enable);
extern int board_nx20p348x_init(int port);
void ppc_interrupt(enum gpio_signal);
void ccd_interrupt(enum gpio_signal);
void bc12_interrupt(enum gpio_signal);

FAKE_VALUE_FUNC(int, ps8743_field_update, const struct usb_mux *, uint8_t,
		uint8_t, uint8_t);
FAKE_VALUE_FUNC(int, ps8743_tune_usb_eq, const struct usb_mux *, uint8_t,
		uint8_t);
FAKE_VALUE_FUNC(enum corsola_db_type, corsola_get_db_type);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VALUE_FUNC(int, rt1718s_get_adc, int, enum rt1718s_adc_channel, int *);
FAKE_VALUE_FUNC(int, tcpci_get_vbus_voltage_no_check, int, int *);
FAKE_VALUE_FUNC(uint8_t, board_get_adjusted_usb_pd_port_count);
FAKE_VOID_FUNC(bmi3xx_interrupt);
FAKE_VOID_FUNC(hdmi_hpd_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(nx20p348x_interrupt, int);
FAKE_VOID_FUNC(ps185_hdmi_hpd_mux_set);
FAKE_VOID_FUNC(usb_charger_task_set_event, int, uint8_t);
FAKE_VOID_FUNC(usb_mux_hpd_update, int, mux_state_t);

#define FFF_FAKES_LIST(FAKE)                       \
	FAKE(bmi3xx_interrupt)                     \
	FAKE(board_get_adjusted_usb_pd_port_count) \
	FAKE(corsola_get_db_type)                  \
	FAKE(hdmi_hpd_interrupt)                   \
	FAKE(nx20p348x_interrupt)                  \
	FAKE(ppc_vbus_sink_enable)                 \
	FAKE(ps185_hdmi_hpd_mux_set)               \
	FAKE(ps8743_field_update)                  \
	FAKE(ps8743_tune_usb_eq)                   \
	FAKE(rt1718s_get_adc)                      \
	FAKE(tcpci_get_vbus_voltage_no_check)      \
	FAKE(usb_charger_task_set_event)           \
	FAKE(usb_mux_hpd_update)

struct kingler_usbc_fixture {
	int place_holder;
};

static const struct device *saved_c1_irq_gpio_port;

static void *kingler_usbc_setup(void)
{
	static struct kingler_usbc_fixture f;

	saved_c1_irq_gpio_port = DEVICE_DT_GET(DT_NODELABEL(gpiod));
	tcpc_config[USBC_PORT_C1].irq_gpio.port = saved_c1_irq_gpio_port;
	return &f;
}

static void kingler_usbc_reset_rule_before(const struct ztest_unit_test *test,
					   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
	tcpc_config[USBC_PORT_C1].irq_gpio.port = saved_c1_irq_gpio_port;
}

static void kingler_usbc_reset_rule_after(const struct ztest_unit_test *test,
					  void *data)
{
	tcpc_config[USBC_PORT_C1].irq_gpio.port = saved_c1_irq_gpio_port;
	for (int i = 0; i < 2; i++) {
		pd_power_supply_reset(i);
	}
}

ZTEST_RULE(kingler_usbc_reset_rule, kingler_usbc_reset_rule_before,
	   kingler_usbc_reset_rule_after);
ZTEST_SUITE(kingler_usbc, NULL, kingler_usbc_setup, NULL, NULL, NULL);

ZTEST_F(kingler_usbc, test_power_supply)
{
	pd_power_supply_reset(0);
	zassert_equal(0, ppc_is_sourcing_vbus(0));
	zassert_equal(0, ppc_is_sourcing_vbus(1));

	zassert_equal(EC_SUCCESS, pd_set_power_supply_ready(0));
	zassert_equal(1, ppc_is_sourcing_vbus(0));
	zassert_equal(0, ppc_is_sourcing_vbus(1));

	pd_power_supply_reset(0);
	zassert_equal(0, ppc_is_sourcing_vbus(0));
	zassert_equal(0, ppc_is_sourcing_vbus(1));

	/* TODO: test C1 port after resolve the PPC emulator always accessing
	 * the same one with different index.
	 */
}

#define FAKE_INPUT_V 5000

int fake_rt1718s_get_adc(int p, enum rt1718s_adc_channel ch, int *v)
{
	*v = FAKE_INPUT_V;
	return 0;
}

int fake_tcpci_get_vbus_voltage_no_check(int p, int *v)
{
	*v = FAKE_INPUT_V;
	return 0;
}

ZTEST(kingler_usbc, test_get_vbus_voltage)
{
	corsola_get_db_type_fake.return_val = CORSOLA_DB_TYPEC;
	zassert_equal(0, charge_manager_get_vbus_voltage(0));
	zassert_equal(0, charge_manager_get_vbus_voltage(1));

	tcpci_get_vbus_voltage_no_check_fake.custom_fake =
		fake_tcpci_get_vbus_voltage_no_check;
	rt1718s_get_adc_fake.custom_fake = fake_rt1718s_get_adc;

	zassert_equal(FAKE_INPUT_V, charge_manager_get_vbus_voltage(0));
	zassert_equal(FAKE_INPUT_V, charge_manager_get_vbus_voltage(1));
}

ZTEST(kingler_usbc, test_board_reset_pd_mcu)
{
	const struct emul *rt1718s_emul =
		EMUL_DT_GET(DT_NODELABEL(rt1718s_emul1));
	uint16_t val;

	board_reset_pd_mcu();
	zassert_equal(rt1718s_emul_get_reg(rt1718s_emul, RT1718S_SYS_CTRL3,
					   &val),
		      EC_SUCCESS);
	/* ensure the mask is cleared */
	zassert_false(val & RT1718S_SWRESET_MASK);
}

ZTEST(kingler_usbc, test_board_set_active_charge_port_invalid)
{
	zassert_equal(EC_ERROR_INVAL, board_set_active_charge_port(5));
	zassert_equal(0, ppc_vbus_sink_enable_fake.call_count);
}

ZTEST(kingler_usbc, test_board_set_active_charge_port_none)
{
	board_get_adjusted_usb_pd_port_count_fake.return_val = 2;
	ppc_vbus_sink_enable_fake.return_val = 0;
	zassert_equal(EC_SUCCESS,
		      board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(2, ppc_vbus_sink_enable_fake.call_count);
	/* C0 */
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg0_history[0]);
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg1_history[0]);
	/* C1 */
	zassert_equal(1, ppc_vbus_sink_enable_fake.arg0_history[1]);
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg1_history[1]);
}

ZTEST(kingler_usbc, test_board_set_active_charge_port_normal)
{
	board_get_adjusted_usb_pd_port_count_fake.return_val = 2;
	ppc_vbus_sink_enable_fake.return_val = 0;

	/* set charge port C0 */
	zassert_equal(EC_SUCCESS, board_set_active_charge_port(0));

	/* disable sink on other ports */
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[0], 1);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[0], 0);

	/* enable sink on port 0 */
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[1], 0);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[1], 1);

	/* enable sink port on port 1 */
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[1], 0);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[1], 1);

	/* set source at C0, and set charge port C0 */
	zassert_equal(EC_SUCCESS, pd_set_power_supply_ready(0));
	zassert_equal(EC_ERROR_INVAL, board_set_active_charge_port(0));
}

ZTEST(kingler_usbc, test_board_vbus_source_enabled)
{
	pd_power_supply_reset(0);
	zassert_equal(board_vbus_source_enabled(0), 0);
	zassert_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
	zassert_equal(board_vbus_source_enabled(0), 1);
}

ZTEST(kingler_usbc, test_bc12_interrupt)
{
	bc12_interrupt(0);
	zassert_equal(usb_charger_task_set_event_fake.call_count, 1);
	zassert_equal(usb_charger_task_set_event_fake.arg0_val, 0);
	zassert_equal(usb_charger_task_set_event_fake.arg1_val,
		      USB_CHG_EVENT_BC12);
}

ZTEST(kingler_usbc, test_ppc_interrupt)
{
	zassert_equal(nx20p348x_interrupt_fake.call_count, 0);

	ppc_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c0_ppc_int_odl)));
	zassert_equal(nx20p348x_interrupt_fake.call_count, 1);
	zassert_equal(nx20p348x_interrupt_fake.arg0_val, 0);

	ppc_interrupt(GPIO_SIGNAL(DT_ALIAS(gpio_usb_c1_ppc_int_odl)));
	zassert_equal(nx20p348x_interrupt_fake.call_count, 2);
	zassert_equal(nx20p348x_interrupt_fake.arg0_val, 1);
}

ZTEST(kingler_usbc, test_board_tcpc_init)
{
	corsola_get_db_type_fake.return_val = CORSOLA_DB_NONE;
	board_tcpc_init();
	zassert_is_null(tcpc_config[USBC_PORT_C1].irq_gpio.port);
	zassert_equal(usb_mux_hpd_update_fake.call_count,
		      CONFIG_USB_PD_PORT_MAX_COUNT);
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(usb_mux_hpd_update_fake.arg0_history[i], i);
		zassert_equal(usb_mux_hpd_update_fake.arg1_history[i],
			      USB_PD_MUX_HPD_LVL_DEASSERTED |
				      USB_PD_MUX_HPD_IRQ_DEASSERTED);
	}
}

ZTEST(kingler_usbc, test_board_rt1718s_init)
{
	const struct emul *rt1718s_emul =
		EMUL_DT_GET(DT_NODELABEL(rt1718s_emul1));
	uint16_t val;

	/* Pre-set BC12 SRC enabled before board_rt1718s_init to verify it
	 * disables it */
	zassert_equal(
		rt1718s_emul_set_reg(rt1718s_emul, RT1718S_RT2_BC12_SRC_FUNC,
				     RT1718S_RT2_BC12_SRC_FUNC_BC12_SRC_EN),
		EC_SUCCESS);

	zassert_equal(EC_SUCCESS, board_rt1718s_init(1));

	/* check gpio1 config */
	zassert_equal(rt1718s_emul_get_reg(rt1718s_emul,
					   RT1718S_GPIO1_VBUS_CTRL, &val),
		      EC_SUCCESS);
	zassert_equal(val & (RT1718S_GPIO_VBUS_CTRL_FRS_RX_VBUS |
			     RT1718S_GPIO_VBUS_CTRL_ENA_SNK_VBUS_GPIO),
		      RT1718S_GPIO_VBUS_CTRL_ENA_SNK_VBUS_GPIO);

	/* check gpio2 config */
	zassert_equal(rt1718s_emul_get_reg(rt1718s_emul,
					   RT1718S_GPIO2_VBUS_CTRL, &val),
		      EC_SUCCESS);
	zassert_equal(val & (RT1718S_GPIO_VBUS_CTRL_FRS_RX_VBUS |
			     RT1718S_GPIO_VBUS_CTRL_ENA_SRC_VBUS_GPIO),
		      (RT1718S_GPIO_VBUS_CTRL_FRS_RX_VBUS |
		       RT1718S_GPIO_VBUS_CTRL_ENA_SRC_VBUS_GPIO));

	/* check frs ctrl3 */
	zassert_equal(rt1718s_emul_get_reg(rt1718s_emul, RT1718S_FRS_CTRL3,
					   &val),
		      EC_SUCCESS);
	zassert_equal(val & (RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO2 |
			     RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO1),
		      (RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO2 |
		       RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO1));

	/* check frs ctrl1 */
	zassert_equal(rt1718s_emul_get_reg(rt1718s_emul, RT1718S_FRS_CTRL1,
					   &val),
		      EC_SUCCESS);
	zassert_equal(val & RT1718S_FRS_CTRL1_FRSWAPRX_MASK,
		      RT1718S_FRS_CTRL1_FRSWAPRX_MASK);

	/* check bc12 src disabled */
	zassert_equal(rt1718s_emul_get_reg(rt1718s_emul,
					   RT1718S_RT2_BC12_SRC_FUNC, &val),
		      EC_SUCCESS);

	zassert_equal(val & RT1718S_RT2_BC12_SRC_FUNC_BC12_SRC_EN, 0);
}

ZTEST(kingler_usbc, test_ps8743_mux_1_board_init)
{
	zassert_equal(EC_SUCCESS, ps8743_mux_1_board_init(usb_muxes[1].mux));
	zassert_equal(1, ps8743_tune_usb_eq_fake.call_count);
	zassert_equal(usb_muxes[1].mux, ps8743_tune_usb_eq_fake.arg0_val);
	zassert_equal(PS8743_USB_EQ_TX_3_6_DB,
		      ps8743_tune_usb_eq_fake.arg1_val);
	zassert_equal(PS8743_USB_EQ_RX_16_0_DB,
		      ps8743_tune_usb_eq_fake.arg2_val);
}

ZTEST(kingler_usbc, test_board_usb_mux_init)
{
	corsola_get_db_type_fake.return_val = CORSOLA_DB_TYPEC;
	board_usb_mux_init();
	zassert_equal(1, ps8743_field_update_fake.call_count);
	zassert_equal(usb_muxes[1].mux, ps8743_field_update_fake.arg0_val);
	zassert_equal(PS8743_REG_DCI_CONFIG_2,
		      ps8743_field_update_fake.arg1_val);
	zassert_equal(PS8743_AUTO_DCI_MODE_MASK,
		      ps8743_field_update_fake.arg2_val);
	zassert_equal(PS8743_AUTO_DCI_MODE_FORCE_USB,
		      ps8743_field_update_fake.arg3_val);

	ps8743_field_update_fake.call_count = 0;
	corsola_get_db_type_fake.return_val = CORSOLA_DB_NONE;
	board_usb_mux_init();
	zassert_equal(0, ps8743_field_update_fake.call_count);
}

ZTEST(kingler_usbc, test_board_tcpc_init_db_typec)
{
	corsola_get_db_type_fake.return_val = CORSOLA_DB_TYPEC;
	board_tcpc_init();
	zassert_not_null(tcpc_config[USBC_PORT_C1].irq_gpio.port);
	zassert_equal(usb_mux_hpd_update_fake.call_count,
		      CONFIG_USB_PD_PORT_MAX_COUNT);
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(usb_mux_hpd_update_fake.arg0_history[i], i);
		zassert_equal(usb_mux_hpd_update_fake.arg1_history[i],
			      USB_PD_MUX_HPD_LVL_DEASSERTED |
				      USB_PD_MUX_HPD_IRQ_DEASSERTED);
	}
}

ZTEST(kingler_usbc, test_board_rt1718s_set_frs_enable)
{
	const struct emul *rt1718s_emul =
		EMUL_DT_GET(DT_NODELABEL(rt1718s_emul1));
	uint16_t val;

	zassert_equal(EC_SUCCESS,
		      board_rt1718s_set_frs_enable(USBC_PORT_C1, 1));
	zassert_equal(rt1718s_emul_get_reg(
			      rt1718s_emul,
			      RT1718S_GPIO_CTRL(GPIO_EN_USB_C1_FRS), &val),
		      EC_SUCCESS);
	zassert_equal(val & RT1718S_GPIO_CTRL_O, RT1718S_GPIO_CTRL_O);

	zassert_equal(EC_SUCCESS,
		      board_rt1718s_set_frs_enable(USBC_PORT_C1, 0));
	zassert_equal(rt1718s_emul_get_reg(
			      rt1718s_emul,
			      RT1718S_GPIO_CTRL(GPIO_EN_USB_C1_FRS), &val),
		      EC_SUCCESS);
	zassert_equal(val & RT1718S_GPIO_CTRL_O, 0);

	/* port != USBC_PORT_C1 should also return EC_SUCCESS */
	zassert_equal(EC_SUCCESS,
		      board_rt1718s_set_frs_enable(USBC_PORT_C0, 1));
}

ZTEST(kingler_usbc, test_board_rt1718s_set_snk_and_src_enable)
{
	const struct emul *rt1718s_emul =
		EMUL_DT_GET(DT_NODELABEL(rt1718s_emul1));
	uint16_t val;

	zassert_equal(EC_SUCCESS,
		      board_rt1718s_set_snk_enable(USBC_PORT_C1, 1));
	zassert_equal(rt1718s_emul_get_reg(
			      rt1718s_emul,
			      RT1718S_GPIO_CTRL(GPIO_EN_USB_C1_SINK), &val),
		      EC_SUCCESS);
	zassert_equal(val & RT1718S_GPIO_CTRL_O, RT1718S_GPIO_CTRL_O);

	zassert_equal(EC_SUCCESS,
		      board_rt1718s_set_snk_enable(USBC_PORT_C1, 0));
	zassert_equal(rt1718s_emul_get_reg(
			      rt1718s_emul,
			      RT1718S_GPIO_CTRL(GPIO_EN_USB_C1_SINK), &val),
		      EC_SUCCESS);
	zassert_equal(val & RT1718S_GPIO_CTRL_O, 0);

	zassert_equal(EC_SUCCESS,
		      board_rt1718s_set_src_enable(USBC_PORT_C1, 1));
	zassert_equal(rt1718s_emul_get_reg(
			      rt1718s_emul,
			      RT1718S_GPIO_CTRL(GPIO_EN_USB_C1_SOURCE), &val),
		      EC_SUCCESS);
	zassert_equal(val & RT1718S_GPIO_CTRL_O, RT1718S_GPIO_CTRL_O);

	zassert_equal(EC_SUCCESS,
		      board_rt1718s_set_src_enable(USBC_PORT_C1, 0));
	zassert_equal(rt1718s_emul_get_reg(
			      rt1718s_emul,
			      RT1718S_GPIO_CTRL(GPIO_EN_USB_C1_SOURCE), &val),
		      EC_SUCCESS);
	zassert_equal(val & RT1718S_GPIO_CTRL_O, 0);
}

ZTEST(kingler_usbc, test_board_set_active_charge_port_sink_enable_fail)
{
	int fail_first_history[] = { 1, 0 };
	int fail_second_history[] = { 0, 1 };

	board_get_adjusted_usb_pd_port_count_fake.return_val = 2;

	/* Disable all ports failure (port == CHARGE_PORT_NONE) */
	ppc_vbus_sink_enable_fake.return_val = 1;
	zassert_equal(EC_SUCCESS,
		      board_set_active_charge_port(CHARGE_PORT_NONE));

	/* Normal charge port: disabling other port fails */
	SET_RETURN_SEQ(ppc_vbus_sink_enable, fail_first_history, 2);
	zassert_equal(EC_SUCCESS, board_set_active_charge_port(0));

	/* Normal charge port: enabling port fails */
	SET_RETURN_SEQ(ppc_vbus_sink_enable, fail_second_history, 2);
	zassert_equal(EC_ERROR_UNKNOWN, board_set_active_charge_port(0));
}

ZTEST(kingler_usbc, test_ppc_interrupt_default)
{
	/* Passing an unknown gpio signal hits the default: branch */
	ppc_interrupt(GPIO_LIMIT);
	zassert_equal(nx20p348x_interrupt_fake.call_count, 0);
}

ZTEST(kingler_usbc, test_board_get_vbus_voltage_errors)
{
	/* C0 get_vbus_voltage returns error */
	tcpci_get_vbus_voltage_no_check_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(0, board_get_vbus_voltage(USBC_PORT_C0));

	/* Default / invalid port */
	zassert_equal(0, board_get_vbus_voltage(CHARGE_PORT_NONE));
	zassert_equal(0, board_get_vbus_voltage(99));
}

ZTEST(kingler_usbc, test_board_nx20p348x_init)
{
	int reg;

	zassert_equal(EC_SUCCESS, board_nx20p348x_init(0));
	zassert_ok(i2c_read8(ppc_chips[0].i2c_port, ppc_chips[0].i2c_addr_flags,
			     NX20P348X_DEVICE_CONTROL_REG, &reg));
	zassert_true((reg & NX20P348X_CTRL_LDO_SD) != 0);

	zassert_equal(EC_SUCCESS, board_nx20p348x_init(1));
	zassert_ok(i2c_read8(ppc_chips[1].i2c_port, ppc_chips[1].i2c_addr_flags,
			     NX20P348X_DEVICE_CONTROL_REG, &reg));
	zassert_true((reg & NX20P348X_CTRL_LDO_SD) != 0);
}

static bool mock_check_vbus_level(int port, enum vbus_level level)
{
	return (port == 0) && (level == VBUS_PRESENT);
}

ZTEST(kingler_usbc, test_pd_snk_is_vbus_provided)
{
	const struct tcpm_drv *saved_drv = tcpc_config[0].drv;
	struct tcpm_drv mock_drv = *saved_drv;

	mock_drv.check_vbus_level = mock_check_vbus_level;
	tcpc_config[0].drv = &mock_drv;

	zassert_true(pd_snk_is_vbus_provided(0));

	tcpc_config[0].drv = saved_drv;
}
