/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "battery.h"
#include "charge_manager.h"
#include "driver/ppc/syv682x.h"
#include "driver/ppc/syv682x_public.h"
#include "driver/tcpm/it83xx_pd.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_syv682x.h"
#include "fakes.h"
#include "gpio_signal.h"
#include "i2c/i2c.h"
#include "power.h"
#include "test_state.h"
#include "usb_pd.h"
#include "usb_pd_policy.h"
#include "usbc_ppc.h"

#include <zephyr/devicetree.h>
#include <zephyr/devicetree/io-channels.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

void xhci_interrupt(enum gpio_signal signal);

static bool ppc_sink_enabled(int port)
{
	const struct emul *emul = (port == 0) ?
					  EMUL_DT_GET(DT_NODELABEL(ppc_port0)) :
					  EMUL_DT_GET(DT_NODELABEL(ppc_port1));
	uint8_t val = 0;

	syv682x_emul_get_reg(emul, SYV682X_CONTROL_1_REG, &val);

	return !(val & (SYV682X_CONTROL_1_PWR_ENB | SYV682X_CONTROL_1_HV_DR));
}

ZTEST(usbc_config, test_set_active_charge_port)
{
	/* reset ppc state */
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);

	/* sourcing port 0, expect port 0 not sinkable */
	zassert_ok(pd_set_power_supply_ready(0));
	zassert_not_equal(board_set_active_charge_port(0), 0, NULL);
	zassert_true(board_vbus_source_enabled(0), NULL);
	zassert_false(board_vbus_source_enabled(1), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);

	/* sinking port 1 */
	zassert_ok(board_set_active_charge_port(1), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_true(ppc_sink_enabled(1), NULL);

	/*
	 * sinking an invalid port should return error and doesn't change
	 * any state
	 */
	zassert_not_equal(board_set_active_charge_port(2), 0, NULL);
	zassert_true(board_vbus_source_enabled(0), NULL);
	zassert_false(board_vbus_source_enabled(1), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_true(ppc_sink_enabled(1), NULL);

	/* turn off sourcing, sinking port 0 */
	pd_power_supply_reset(0);
	zassert_ok(board_set_active_charge_port(0), NULL);
	zassert_true(ppc_sink_enabled(0), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);

	/* sinking port 1 */
	zassert_ok(board_set_active_charge_port(1), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_true(ppc_sink_enabled(1), NULL);

	/* back to port 0 */
	zassert_ok(board_set_active_charge_port(0), NULL);
	zassert_true(ppc_sink_enabled(0), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);

	/* reset */
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE), NULL);
	zassert_false(board_vbus_source_enabled(0), NULL);
	zassert_false(board_vbus_source_enabled(1), NULL);
	zassert_false(ppc_sink_enabled(0), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);
}

ZTEST(usbc_config, test_set_active_charge_port_fail)
{
	const struct emul *ppc0 = EMUL_DT_GET(DT_NODELABEL(ppc_port0));

	/* Verify that failure on ppc0 doesn't affect ppc1 */
	i2c_common_emul_set_write_fail_reg(
		emul_syv682x_get_i2c_common_data(ppc0),
		I2C_COMMON_EMUL_FAIL_ALL_REG);

	zassert_ok(board_set_active_charge_port(1), NULL);
	zassert_true(ppc_sink_enabled(1), NULL);
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE), NULL);
	zassert_false(ppc_sink_enabled(1), NULL);
	zassert_ok(board_set_active_charge_port(1), NULL);
	zassert_true(ppc_sink_enabled(1), NULL);

	/* trying to enable ppc0 results in error */
	zassert_not_equal(board_set_active_charge_port(0), 0, NULL);
	zassert_false(ppc_sink_enabled(1), NULL);
}

ZTEST(usbc_config, test_adc_channel)
{
	zassert_equal(board_get_vbus_adc(0), ADC_VBUS_C0, NULL);
	zassert_equal(board_get_vbus_adc(1), ADC_VBUS_C1, NULL);
	zassert_equal(board_get_vbus_adc(99), ADC_VBUS_C0, NULL);
}

static void set_vbus_adc(int voltage)
{
	const struct device *adc_dev =
		DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(DT_NODELABEL(adc_vbus_c0)));
	const int channel = DT_IO_CHANNELS_INPUT(DT_NODELABEL(adc_vbus_c0));

	adc_emul_const_value_set(adc_dev, channel, voltage / 10);
}

ZTEST(usbc_config, test_pd_check_vbus_level)
{
	/* SAFE0V true */
	set_vbus_adc(PD_V_SAFE0V_MAX - 1);
	zassert_true(pd_check_vbus_level(0, VBUS_SAFE0V), NULL);

	/* SAFE0V false */
	set_vbus_adc(PD_V_SAFE0V_MAX + 100);
	zassert_false(pd_check_vbus_level(0, VBUS_SAFE0V), NULL);

	/* PRESENT true */
	set_vbus_adc(PD_V_SAFE5V_MIN + 100);
	zassert_true(pd_check_vbus_level(0, VBUS_PRESENT), NULL);

	/* PRESENT false */
	set_vbus_adc(PD_V_SAFE5V_MIN - 500);
	zassert_false(pd_check_vbus_level(0, VBUS_PRESENT), NULL);

	/* REMOVED true */
	set_vbus_adc(PD_V_SINK_DISCONNECT_MAX - 1);
	zassert_true(pd_check_vbus_level(0, VBUS_REMOVED), NULL);

	/* REMOVED false */
	set_vbus_adc(PD_V_SINK_DISCONNECT_MAX + 500);
	zassert_false(pd_check_vbus_level(0, VBUS_REMOVED), NULL);

	/* invalid enum */
	set_vbus_adc(1000);
	zassert_false(pd_check_vbus_level(0, 123), NULL);
}

ZTEST(usbc_config, test_board_pd_misc)
{
	board_pd_vconn_ctrl(0, USBPD_CC_PIN_1, 1);
	board_reset_pd_mcu();
}

#ifdef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
ZTEST(usbc_config, test_cc_tuning_parameter)
{
	const struct cc_para_t *param;

	param = board_get_cc_tuning_parameter(0);
	zassert_not_null(param, NULL);
	zassert_equal(param->rc_filter, IT83XX_TX_RC_FILTER_TRIM, NULL);

	param = board_get_cc_tuning_parameter(1);
	zassert_not_null(param, NULL);
	zassert_equal(param->rc_filter, IT83XX_TX_RC_FILTER_TRIM, NULL);
}
#endif

ZTEST(usbc_config, test_pd_get_drp_state_in_s0)
{
	const struct gpio_dt_spec *xhci =
		GPIO_DT_FROM_NODELABEL(ap_xhci_init_done);

	gpio_emul_input_set(xhci->port, xhci->pin, 1);
	zassert_equal(pd_get_drp_state_in_s0(), PD_DRP_TOGGLE_ON, NULL);

	gpio_emul_input_set(xhci->port, xhci->pin, 0);
	zassert_equal(pd_get_drp_state_in_s0(), PD_DRP_FORCE_SINK, NULL);
}

ZTEST(usbc_config, test_xhci_interrupt)
{
	const struct gpio_dt_spec *xhci =
		GPIO_DT_FROM_NODELABEL(ap_xhci_init_done);
	enum gpio_signal signal = GPIO_SIGNAL(DT_NODELABEL(ap_xhci_init_done));

	/* Input 1, attached src=0 -> dual role PD_DRP_TOGGLE_ON */
	gpio_emul_input_set(xhci->port, xhci->pin, 1);
	tc_is_attached_src_fake.return_val = 0;
	xhci_interrupt(signal);
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(pd_get_dual_role(i), PD_DRP_TOGGLE_ON);
	}

	/* Input 1, attached src=1 -> dual role PD_DRP_TOGGLE_ON */
	gpio_emul_input_set(xhci->port, xhci->pin, 1);
	tc_is_attached_src_fake.return_val = 1;
	xhci_interrupt(signal);
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(pd_get_dual_role(i), PD_DRP_TOGGLE_ON);
	}

	/* Input 0 with attached src -> dual role PD_DRP_FORCE_SINK */
	gpio_emul_input_set(xhci->port, xhci->pin, 0);
	tc_is_attached_src_fake.return_val = 1;
	xhci_interrupt(signal);
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(pd_get_dual_role(i), PD_DRP_FORCE_SINK);
	}

	/* Input 0 with not attached src */
	tc_is_attached_src_fake.return_val = 0;
	xhci_interrupt(signal);
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(pd_get_dual_role(i), PD_DRP_FORCE_SINK);
	}
}

ZTEST(usbc_config, test_pd_check_vconn_swap)
{
	const int port = 0;

	/* suspend */
	power_set_state(POWER_S3);
	zassert_equal(pd_check_vconn_swap(port), true, NULL);

	/* s0 */
	power_set_state(POWER_S0);
	zassert_equal(pd_check_vconn_swap(port), true, NULL);

	/* softoff */
	power_set_state(POWER_S5);
	zassert_equal(pd_check_vconn_swap(port), false, NULL);

	/* hardoff */
	power_set_state(POWER_G3);
	zassert_equal(pd_check_vconn_swap(port), false, NULL);
}

ZTEST(usbc_config, test_pd_snk_is_vbus_provided)
{
	set_vbus_adc(PD_V_SINK_DISCONNECT_MAX + 500);
	zassert_equal(pd_snk_is_vbus_provided(0), 1, NULL);
	zassert_equal(pd_snk_is_vbus_provided(0), 1, NULL);

	set_vbus_adc(PD_V_SINK_DISCONNECT_MAX - 500);
	zassert_equal(pd_snk_is_vbus_provided(0), 0, NULL);
	zassert_equal(pd_snk_is_vbus_provided(0), 0, NULL);
}

static int fail_second_write(const struct emul *target, int reg, uint8_t val,
			     int bytes, void *data)
{
	int *write_count = data;

	(*write_count)++;
	if (*write_count >= 2) {
		return -EIO;
	}
	return 1;
}

ZTEST(usbc_config, test_pd_set_power_supply_ready_fail)
{
	const struct emul *ppc0 = EMUL_DT_GET(DT_NODELABEL(ppc_port0));
	struct i2c_common_emul_data *i2c_data =
		emul_syv682x_get_i2c_common_data(ppc0);
	int write_count = 0;

	/* Fail sink disable */
	i2c_common_emul_set_write_fail_reg(i2c_data,
					   I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS, NULL);

	/* Restore and fail source enable */
	i2c_common_emul_set_write_fail_reg(i2c_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_func(i2c_data, fail_second_write,
				       &write_count);
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS, NULL);

	i2c_common_emul_set_write_func(i2c_data, NULL, NULL);
}

static void geralt_usbc_config_before(void *fixture)
{
	const struct emul *ppc0 = EMUL_DT_GET(DT_NODELABEL(ppc_port0));
	const struct emul *ppc1 = EMUL_DT_GET(DT_NODELABEL(ppc_port1));

	RESET_FAKE(tc_is_attached_src);
	power_set_state(POWER_S0);

	i2c_common_emul_set_write_fail_reg(
		emul_syv682x_get_i2c_common_data(ppc0),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_syv682x_get_i2c_common_data(ppc1),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_func(emul_syv682x_get_i2c_common_data(ppc0),
				       NULL, NULL);
	i2c_common_emul_set_write_func(emul_syv682x_get_i2c_common_data(ppc1),
				       NULL, NULL);

	board_set_active_charge_port(CHARGE_PORT_NONE);
}

ZTEST_SUITE(usbc_config, geralt_predicate_post_main, NULL,
	    geralt_usbc_config_before, NULL, NULL);
