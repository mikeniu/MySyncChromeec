/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_events.h"
#include "battery.h"
#include "charger.h"
#include "driver/tcpm/tcpci.h"
#include "emul/emul_sm5803.h"
#include "emul/tcpc/emul_tcpci.h"
#include "hooks.h"
#include "ocpc.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <dt-bindings/gpio_defines.h>

FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
FAKE_VALUE_FUNC(int, board_set_active_charge_port, int);
FAKE_VALUE_FUNC(uint8_t, board_get_usb_pd_port_count);
FAKE_VALUE_FUNC(int, power_button_is_pressed);

#define TCPC0 EMUL_DT_GET(DT_NODELABEL(tcpc_port0))
#define TCPC1 EMUL_DT_GET(DT_NODELABEL(tcpc_port1))

static void suite_before(void *fixture)
{
	RESET_FAKE(battery_is_present);
	RESET_FAKE(board_get_usb_pd_port_count);
	board_get_usb_pd_port_count_fake.return_val = 2;
	RESET_FAKE(board_set_active_charge_port);
	RESET_FAKE(power_button_is_pressed);
}

ZTEST_SUITE(nissa_common, NULL, NULL, suite_before, NULL, NULL);

ZTEST(nissa_common, test_pen_power_control)
{
	const struct gpio_dt_spec *const pen_power =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x);

	hook_notify(HOOK_INIT);
	zassert_false(gpio_emul_output_get(pen_power->port, pen_power->pin),
		      "Pen power should be off by default");

	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	zassert_true(gpio_emul_output_get(pen_power->port, pen_power->pin),
		     "Pen power should be on after AP startup");

	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	zassert_false(gpio_emul_output_get(pen_power->port, pen_power->pin),
		      "Pen power should be off after AP shutdown");
}

ZTEST(nissa_common, test_hibernate)
{
	const struct gpio_dt_spec *const hibernate_enable =
		GPIO_DT_FROM_NODELABEL(gpio_en_slp_z);

	zassert_false(gpio_emul_output_get(hibernate_enable->port,
					   hibernate_enable->pin),
		      "Hibernate pin should be low by default");
	board_hibernate_late();
	zassert_true(gpio_emul_output_get(hibernate_enable->port,
					  hibernate_enable->pin),
		     "Hibernate pin should go high to hibernate");
}

ZTEST(nissa_common, test_vconn_swap)
{
	const struct gpio_dt_spec *const dsw_pwrok =
		GPIO_DT_FROM_NODELABEL(gpio_ec_soc_dsw_pwrok);

	/* AP 5V rail is off. */
	zassert_false(gpio_pin_get_dt(dsw_pwrok));
	zassert_false(pd_check_vconn_swap(0));
	zassert_false(pd_check_vconn_swap(1));

	/*
	 * Case with the rail on is untestable because emulated GPIOs don't
	 * allow getting the current value of output pins.
	 */
}

ZTEST(nissa_common, test_ocpc_configuration)
{
	int kp, kp_div;
	int ki, ki_div;
	int kd, kd_div;
	struct ocpc_data ocpc_data = {};

	ocpc_get_pid_constants(&kp, &kp_div, &ki, &ki_div, &kd, &kd_div);

	/*
	 * Only proportional control is used, at 1/32 gain. Gain of integral and
	 * derivative terms is zero.
	 */
	zassert_equal(kp, 1);
	zassert_equal(kp_div, 32);
	zassert_equal(ki, 0);
	zassert_not_equal(ki_div, 0);
	zassert_equal(kd, 0);
	zassert_not_equal(kd_div, 0);

	/* With two chargers, we note that Isys can't be measured. */
	zassert_equal(CONFIG_USB_PD_PORT_MAX_COUNT, 2);
	board_get_usb_pd_port_count_fake.return_val = 2;
	board_ocpc_init(&ocpc_data);
	zassert_equal(ocpc_data.chg_flags[1], OCPC_NO_ISYS_MEAS_CAP);
}

void board_get_battery_cells(void);

ZTEST(nissa_common, test_sm5803_buck_boost_forbidden)
{
	const struct emul *const charger_emul =
		EMUL_DT_GET(DT_NODELABEL(chg_port0));
	int cells;

	/* Default 2S PMODE allows 12V charging. */
	zassert_ok(charger_get_battery_cells(0, &cells));
	zassert_equal(cells, 2);
	zassert_true(pd_is_valid_input_voltage(12000));

	/* 3S forbids 12V charging. */
	sm5803_emul_set_pmode(charger_emul, 0x16 /* 3S, 1.5A with BFET */);
	zassert_ok(charger_get_battery_cells(0, &cells));
	zassert_equal(cells, 3);
	board_get_battery_cells(); /* Refresh cached cell count */
	zassert_false(pd_is_valid_input_voltage(12000));
}

ZTEST(nissa_common, test_i2c_passthru_policy)
{
	/* Type-C ports are allowed */
	zassert_true(board_allow_i2c_passthru(&(struct i2c_cmd_desc_t){
		.port = I2C_PORT_USB_C0_TCPC,
	}));
	zassert_true(board_allow_i2c_passthru(&(struct i2c_cmd_desc_t){
		.port = I2C_PORT_USB_C1_TCPC,
	}));

	/* Others are not */
	zassert_false(board_allow_i2c_passthru(&(struct i2c_cmd_desc_t){
		.port = I2C_PORT_BATTERY,
	}));
}

ZTEST(nissa_common, test_tcpc_get_alert_status)
{
	const struct gpio_dt_spec *c0_int =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl);
	const struct gpio_dt_spec *c1_int =
		GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl);

	/* Sub-board IO configuration is handled by other inits */
	gpio_pin_configure_dt(c1_int, GPIO_INPUT_PULL_UP);

	/* Both IRQs are asserted */
	gpio_emul_input_set(c0_int->port, c0_int->pin, 0);
	gpio_emul_input_set(c1_int->port, c1_int->pin, 0);

	/* Enable all TCPC Alerts */
	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT_MASK, TCPC_REG_ALERT_MASK_ALL);
	tcpci_emul_set_reg(TCPC1, TCPC_REG_ALERT_MASK, TCPC_REG_ALERT_MASK_ALL);

	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT, 1);
	zassert_equal(tcpc_get_alert_status(), PD_STATUS_TCPC_ALERT_0);

	/* Bit 14 is ignored */
	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT, 0x4000);
	zassert_equal(tcpc_get_alert_status(), 0);

	/* Port 1 works too */
	tcpci_emul_set_reg(TCPC1, TCPC_REG_ALERT, 0x8000);
	zassert_equal(tcpc_get_alert_status(), PD_STATUS_TCPC_ALERT_1);

	/* Port 1 ignored when port count is 1 */
	board_get_usb_pd_port_count_fake.return_val = 1;
	zassert_equal(tcpc_get_alert_status(), 0);
	board_get_usb_pd_port_count_fake.return_val = 2;

	/* Disable all TCPC Alerts */
	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT_MASK, 0);
	tcpci_emul_set_reg(TCPC1, TCPC_REG_ALERT_MASK, 0);

	/* Expect no alert status when alert mask is 0 */
	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT, 1);
	zassert_equal(tcpc_get_alert_status(), 0);

	tcpci_emul_set_reg(TCPC1, TCPC_REG_ALERT, 0x8000);
	zassert_equal(tcpc_get_alert_status(), 0);
}
