/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_events.h"
#include "baseboard_usbc_config.h"
#include "driver/tcpm/tcpm.h"
#include "ec_app_main.h"
#include "gpio.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "test/drivers/utils.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "variant_db_detection.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

void x_ec_interrupt(enum gpio_signal signal);

#define LOG_LEVEL 0
LOG_MODULE_REGISTER(corsola_usbc);

FAKE_VOID_FUNC(ppc_interrupt, enum gpio_signal);
FAKE_VALUE_FUNC(enum corsola_db_type, corsola_get_db_type);
DECLARE_FAKE_VOID_FUNC(bmi3xx_interrupt);
FAKE_VOID_FUNC(hdmi_hpd_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(ps185_hdmi_hpd_mux_set);
FAKE_VALUE_FUNC(bool, ps8743_field_update, const struct usb_mux *, uint8_t,
		uint8_t, uint8_t);
FAKE_VALUE_FUNC(int, tc_is_attached_src, int);
FAKE_VALUE_FUNC(int, usb_charge_set_mode, int, enum usb_charge_mode,
		enum usb_suspend_charge);
FAKE_VALUE_FUNC(int, board_set_active_charge_port, int);
FAKE_VOID_FUNC(pd_power_supply_reset, int);
FAKE_VALUE_FUNC(int, pd_check_vconn_swap, int);
FAKE_VALUE_FUNC(int, pd_set_power_supply_ready, int);
FAKE_VOID_FUNC(bc12_interrupt, enum gpio_signal);

#define FFF_FAKES_LIST(FAKE)               \
	FAKE(corsola_get_db_type)          \
	FAKE(bmi3xx_interrupt)             \
	FAKE(hdmi_hpd_interrupt)           \
	FAKE(ps185_hdmi_hpd_mux_set)       \
	FAKE(ps8743_field_update)          \
	FAKE(usb_charge_set_mode)          \
	FAKE(ppc_interrupt)                \
	FAKE(board_set_active_charge_port) \
	FAKE(pd_power_supply_reset)        \
	FAKE(pd_check_vconn_swap)          \
	FAKE(pd_set_power_supply_ready)    \
	FAKE(bc12_interrupt)               \
	FAKE(tc_is_attached_src)

extern void xhci_interrupt(enum gpio_signal signal);
extern void baseboard_init(void);
extern void baseboard_x_ec_gpio2_init(void);

static int get_gpio_output(const struct gpio_dt_spec *const spec)
{
	return gpio_emul_output_get(spec->port, spec->pin);
}

ZTEST(corsola_usbc, test_xhci_interrupt_0_src_attached)
{
	const struct gpio_dt_spec *xhci =
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done);

	gpio_emul_input_set(xhci->port, xhci->pin, 0);
	tc_is_attached_src_fake.return_val = true;
	xhci_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_ap_xhci_init_done)));
	k_msleep(5);

	for (int i = 0; i < USB_PORT_COUNT; i++) {
		zassert_equal(usb_charge_set_mode_fake.arg0_history[i], i);
		zassert_equal(usb_charge_set_mode_fake.arg1_history[i],
			      USB_CHARGE_MODE_DISABLED);
		zassert_equal(usb_charge_set_mode_fake.arg2_history[i],
			      USB_ALLOW_SUSPEND_CHARGE);
	}
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(pd_get_dual_role(i), PD_DRP_FORCE_SINK);
	}
}

ZTEST(corsola_usbc, test_xhci_interrupt_0)
{
	const struct gpio_dt_spec *xhci =
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done);

	gpio_emul_input_set(xhci->port, xhci->pin, 0);
	tc_is_attached_src_fake.return_val = false;
	xhci_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_ap_xhci_init_done)));
	k_msleep(5);

	for (int i = 0; i < USB_PORT_COUNT; i++) {
		zassert_equal(usb_charge_set_mode_fake.arg0_history[i], i);
		zassert_equal(usb_charge_set_mode_fake.arg1_history[i],
			      USB_CHARGE_MODE_DISABLED);
		zassert_equal(usb_charge_set_mode_fake.arg2_history[i],
			      USB_ALLOW_SUSPEND_CHARGE);
	}
}

ZTEST(corsola_usbc, test_xhci_interrupt_1)
{
	const struct gpio_dt_spec *xhci =
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done);

	gpio_emul_input_set(xhci->port, xhci->pin, 1);
	xhci_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_ap_xhci_init_done)));
	k_msleep(5);

	for (int i = 0; i < USB_PORT_COUNT; i++) {
		zassert_equal(usb_charge_set_mode_fake.arg0_history[i], i);
		zassert_equal(usb_charge_set_mode_fake.arg1_history[i],
			      USB_CHARGE_MODE_ENABLED);
		zassert_equal(usb_charge_set_mode_fake.arg2_history[i],
			      USB_ALLOW_SUSPEND_CHARGE);
	}
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(pd_get_dual_role(i), PD_DRP_TOGGLE_ON);
	}
}

ZTEST(corsola_usbc, test_x_ec_interrupt)
{
	const struct gpio_dt_spec *x_ec =
		GPIO_DT_FROM_NODELABEL(gpio_x_ec_gpio2);

	corsola_get_db_type_fake.return_val = CORSOLA_DB_TYPEC;
	gpio_emul_input_set(x_ec->port, x_ec->pin, 1);
	x_ec_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_x_ec_gpio2)));
	zassert_equal(0, hdmi_hpd_interrupt_fake.call_count);
	zassert_equal(1, ppc_interrupt_fake.call_count);

	corsola_get_db_type_fake.return_val = CORSOLA_DB_HDMI;
	x_ec_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_x_ec_gpio2)));
	zassert_equal(1, hdmi_hpd_interrupt_fake.call_count);
	zassert_equal(1, ppc_interrupt_fake.call_count);

	corsola_get_db_type_fake.return_val = CORSOLA_DB_NONE;
	x_ec_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_x_ec_gpio2)));
	zassert_equal(1, hdmi_hpd_interrupt_fake.call_count);
	zassert_equal(1, ppc_interrupt_fake.call_count);
}

ZTEST(corsola_usbc, test_pd_get_drp_state_in_s0)
{
	const struct gpio_dt_spec *xhci =
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done);

	gpio_emul_input_set(xhci->port, xhci->pin, 1);
	zassert_equal(pd_get_drp_state_in_s0(), PD_DRP_TOGGLE_ON);

	gpio_emul_input_set(xhci->port, xhci->pin, 0);
	zassert_equal(pd_get_drp_state_in_s0(), PD_DRP_FORCE_SINK);
}

ZTEST(corsola_usbc, test_baseboard_init)
{
	int flags;

	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done), &flags));
	zassert_equal(flags & GPIO_INT_ENABLE, 0, "actual GPIO flags were %#x",
		      flags);

	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ccd_mode_odl), &flags));
	zassert_equal(flags & GPIO_INT_ENABLE, 0, "actual GPIO flags were %#x",
		      flags);

	baseboard_init();

	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done), &flags));
	zassert_equal(flags & GPIO_INT_ENABLE, GPIO_INT_ENABLE,
		      "actual GPIO flags were %#x", flags);

	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ccd_mode_odl), &flags));
	zassert_equal(flags & GPIO_INT_ENABLE, GPIO_INT_ENABLE,
		      "actual GPIO flags were %#x", flags);
}

extern bool tasks_inited;

ZTEST(corsola_usbc, test_baseboard_x_ec_gpio2_init)
{
	int flags;

	/* no board */
	corsola_get_db_type_fake.return_val = CORSOLA_DB_NONE;
	baseboard_x_ec_gpio2_init();
	k_sleep(K_SECONDS(1));
	zassert_equal(tasks_inited, false);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_x_ec_gpio2), &flags));
	zassert_equal(flags & GPIO_INT_ENABLE, 0, "actual GPIO flags were %#x",
		      flags);
	gpio_reset(GPIO_SIGNAL(DT_NODELABEL(gpio_x_ec_gpio2)));

	/* type-c board */
	corsola_get_db_type_fake.return_val = CORSOLA_DB_TYPEC;
	baseboard_x_ec_gpio2_init();
	k_sleep(K_SECONDS(1));
	zassert_equal(tasks_inited, false);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_x_ec_gpio2), &flags));
	zassert_equal(flags & (GPIO_INT_ENABLE | GPIO_INT_EDGE_FALLING),
		      GPIO_INT_ENABLE | GPIO_INT_EDGE_FALLING,
		      "actual GPIO flags were %#x", flags);
	gpio_reset(GPIO_SIGNAL(DT_NODELABEL(gpio_x_ec_gpio2)));

	/* hdmi board */
	corsola_get_db_type_fake.return_val = CORSOLA_DB_HDMI;
	baseboard_x_ec_gpio2_init();
	k_sleep(K_SECONDS(1));
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_x_ec_gpio2), &flags));
	zassert_equal(flags & GPIO_INT_ENABLE, GPIO_INT_ENABLE,
		      "actual GPIO flags were %#x", flags);
	zassert_equal(tasks_inited, true);

	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	k_sleep(K_SECONDS(1));
	zassert_equal(get_gpio_output(GPIO_DT_FROM_ALIAS(gpio_en_hdmi_pwr)), 0);
	zassert_equal(get_gpio_output(GPIO_DT_FROM_ALIAS(gpio_ps185_pwrdn_odl)),
		      0);

	ap_power_ev_send_callbacks(AP_POWER_RESUME);
	k_sleep(K_SECONDS(1));
	zassert_equal(get_gpio_output(GPIO_DT_FROM_ALIAS(gpio_en_hdmi_pwr)), 1);
	zassert_equal(get_gpio_output(GPIO_DT_FROM_ALIAS(gpio_ps185_pwrdn_odl)),
		      1);
}

ZTEST(corsola_usbc, test_board_get_usb_pd_port_count)
{
	corsola_get_db_type_fake.return_val = CORSOLA_DB_HDMI;
	tasks_inited = false;
	zassert_equal(board_get_usb_pd_port_count(),
		      CONFIG_USB_PD_PORT_MAX_COUNT - 1);
	tasks_inited = true;
	zassert_equal(board_get_usb_pd_port_count(),
		      CONFIG_USB_PD_PORT_MAX_COUNT);

	corsola_get_db_type_fake.return_val = CORSOLA_DB_NONE;
	zassert_equal(board_get_usb_pd_port_count(),
		      CONFIG_USB_PD_PORT_MAX_COUNT - 1);

	corsola_get_db_type_fake.return_val = CORSOLA_DB_TYPEC;
	zassert_equal(board_get_usb_pd_port_count(),
		      CONFIG_USB_PD_PORT_MAX_COUNT);
}

ZTEST(corsola_usbc, test_board_pd_port_num_is_valid)
{
	corsola_get_db_type_fake.return_val = CORSOLA_DB_HDMI;
	zassert_true(board_pd_port_num_is_valid(0));
	zassert_false(board_pd_port_num_is_valid(1));
	zassert_false(board_pd_port_num_is_valid(-1));

	corsola_get_db_type_fake.return_val = CORSOLA_DB_TYPEC;
	zassert_true(board_pd_port_num_is_valid(0));
	zassert_true(board_pd_port_num_is_valid(1));
	zassert_false(board_pd_port_num_is_valid(2));
	zassert_false(board_pd_port_num_is_valid(-1));

	corsola_get_db_type_fake.return_val = CORSOLA_DB_NO_DETECTION;
	zassert_equal(board_get_adjusted_usb_pd_port_count(),
		      CONFIG_USB_PD_PORT_MAX_COUNT);
	zassert_true(board_pd_port_num_is_valid(1));
}

ZTEST(corsola_usbc, test_board_pd_vconn_ctrl)
{
	board_pd_vconn_ctrl(0, USBPD_CC_PIN_1, 1);
	board_pd_vconn_ctrl(1, USBPD_CC_PIN_2, 0);
}

ZTEST(corsola_usbc, test_board_hdmi_handler)
{
	/* Type-C DB returns early */
	corsola_get_db_type_fake.return_val = CORSOLA_DB_TYPEC;
	ap_power_ev_send_callbacks(AP_POWER_RESUME);

	/* Trigger switch default branch */
	corsola_get_db_type_fake.return_val = CORSOLA_DB_HDMI;
	STRUCT_SECTION_FOREACH(ap_power_ev_callback, cb)
	{
		struct ap_power_ev_data data = { .event = AP_POWER_PRE_INIT };
		cb->handler((struct ap_power_ev_callback *)cb, data);
	}
}

ZTEST(corsola_usbc, test_get_dp_pin_mode)
{
	corsola_get_db_type_fake.return_val = CORSOLA_DB_HDMI;
	usb_mux_set(USBC_PORT_C1, USB_PD_MUX_DP_ENABLED, USB_SWITCH_CONNECT, 0);
	zassert_equal(get_dp_pin_mode(USBC_PORT_C1), MODE_DP_PIN_E);

	usb_mux_set(USBC_PORT_C1, USB_PD_MUX_NONE, USB_SWITCH_CONNECT, 0);
	zassert_equal(get_dp_pin_mode(USBC_PORT_C1), 0);

	/* Fallback pd_dfp_dp_get_pin_mode */
	get_dp_pin_mode(USBC_PORT_C0);

	corsola_get_db_type_fake.return_val = CORSOLA_DB_TYPEC;
	get_dp_pin_mode(USBC_PORT_C1);
}

static void corsola_reset(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Reset fakes */
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
}

ZTEST_SUITE(corsola_usbc, NULL, NULL, corsola_reset, corsola_reset, NULL);
