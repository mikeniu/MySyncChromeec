/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/charger/bq25710.h"
#include "driver/charger/bq257x0_regs.h"
#include "fan.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "i2c.h"
#include "rauru_sub_board.h"
#include "system.h"
#include "test_state.h"
#include "usb_pd.h"
#include "usb_tc_sm.h"
#include "usbc/usb_muxes.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/* Forward declarations */
void xhci_interrupt(enum gpio_signal signal);
void board_hibernate_late(void);
enum pd_dual_role_states pd_get_drp_state_in_s0(void);
void update_bq25720_input_voltage(void);

/* Test exports from hooks.c and sub_board.c */
extern void rauru_common_init(void);
extern enum rauru_sub_board_type rauru_cached_sub_board;

const struct charger_config_t chg_chips[] = {
	[CHARGER_SOLO] = {
		.i2c_port = I2C_PORT_POWER,
		.i2c_addr_flags = 0x09,
	},
};

/* FFF declarations */
DECLARE_FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config,
			enum cbi_fw_config_field_id, uint32_t *);
FAKE_VOID_FUNC(fan_set_rpm_mode, int, int);
FAKE_VOID_FUNC(fan_set_rpm_target, int, int);

/* Weak override for tc_is_attached_src */
static int mock_tc_is_attached_src;
int tc_is_attached_src(int port)
{
	return mock_tc_is_attached_src;
}

static int cbi_sb_none(enum cbi_fw_config_field_id id, uint32_t *val)
{
	ARG_UNUSED(id);
	*val = FW_SB_NONE;
	return 0;
}

static int cbi_sb_redriver(enum cbi_fw_config_field_id id, uint32_t *val)
{
	ARG_UNUSED(id);
	*val = FW_SB_REDRIVER;
	return 0;
}

static int cbi_sb_retimer(enum cbi_fw_config_field_id id, uint32_t *val)
{
	ARG_UNUSED(id);
	*val = FW_SB_RETIMER;
	return 0;
}

static int cbi_sb_unknown(enum cbi_fw_config_field_id id, uint32_t *val)
{
	ARG_UNUSED(id);
	*val = 0xFF;
	return 0;
}

static struct usb_mux_chain orig_usb_mux_1;
static bool orig_mux_saved;

static void test_hooks_reset(void *fixture)
{
	ARG_UNUSED(fixture);

	if (!orig_mux_saved) {
		orig_usb_mux_1 = usb_muxes[1];
		orig_mux_saved = true;
	} else {
		usb_muxes[1] = orig_usb_mux_1;
	}

	RESET_FAKE(fan_set_rpm_mode);
	RESET_FAKE(fan_set_rpm_target);
	cros_cbi_get_fw_config_fake.return_val = 0;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_sb_none;

	rauru_cached_sub_board = RAURU_SB_UNKNOWN;
	mock_tc_is_attached_src = false;
}

/* ========================================================================= */
/* sub_board.c Unit Tests                                                    */
/* ========================================================================= */

ZTEST(hooks_test, test_rauru_get_sb_type_cached)
{
	rauru_cached_sub_board = RAURU_SB_REDRIVER;
	zassert_equal(rauru_get_sb_type(), RAURU_SB_REDRIVER);
}

ZTEST(hooks_test, test_rauru_get_sb_type_error)
{
	rauru_cached_sub_board = RAURU_SB_UNKNOWN;
	cros_cbi_get_fw_config_fake.custom_fake = NULL;
	cros_cbi_get_fw_config_fake.return_val = -1;
	zassert_equal(rauru_get_sb_type(), RAURU_SB_NONE);
	zassert_equal(rauru_cached_sub_board, RAURU_SB_NONE);
}

ZTEST(hooks_test, test_rauru_get_sb_type_default)
{
	rauru_cached_sub_board = RAURU_SB_UNKNOWN;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_sb_unknown;
	zassert_equal(rauru_get_sb_type(), RAURU_SB_NONE);
	zassert_equal(rauru_cached_sub_board, RAURU_SB_NONE);
}

ZTEST(hooks_test, test_rauru_get_sb_type_none)
{
	rauru_cached_sub_board = RAURU_SB_UNKNOWN;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_sb_none;
	zassert_equal(rauru_get_sb_type(), RAURU_SB_NONE);
	zassert_equal(rauru_cached_sub_board, RAURU_SB_NONE);
}

ZTEST(hooks_test, test_rauru_get_sb_type_redriver)
{
	rauru_cached_sub_board = RAURU_SB_UNKNOWN;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_sb_redriver;
	zassert_equal(rauru_get_sb_type(), RAURU_SB_REDRIVER);
	zassert_equal(rauru_cached_sub_board, RAURU_SB_REDRIVER);
}

ZTEST(hooks_test, test_rauru_get_sb_type_retimer)
{
	rauru_cached_sub_board = RAURU_SB_UNKNOWN;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_sb_retimer;
	zassert_equal(rauru_get_sb_type(), RAURU_SB_RETIMER);
	zassert_equal(rauru_cached_sub_board, RAURU_SB_RETIMER);
}

/* ========================================================================= */
/* hibernate.c Unit Tests                                                    */
/* ========================================================================= */

ZTEST(hooks_test, test_board_hibernate_late)
{
	const struct gpio_dt_spec *en_5v =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_z1);
	const struct gpio_dt_spec *en_ulp = GPIO_DT_FROM_NODELABEL(gpio_en_ulp);

	/* Initialize to pre-hibernate states */
	gpio_pin_set_dt(en_5v, 1);
	gpio_pin_set_dt(en_ulp, 0);

	board_hibernate_late();

	/* PP5000 5V line must be shut off (0) */
	zassert_equal(gpio_emul_output_get(en_5v->port, en_5v->pin), 0);
	/* ULP rail must be enabled (1) */
	zassert_equal(gpio_emul_output_get(en_ulp->port, en_ulp->pin), 1);
}

/* ========================================================================= */
/* hooks.c Unit Tests                                                        */
/* ========================================================================= */

ZTEST(hooks_test, test_rauru_common_init)
{
	int reg_val;

	/* Pre-set EXTILIM bit in charge option 2 register */
	i2c_write16(I2C_PORT_POWER, 0x09, BQ25710_REG_CHARGE_OPTION_2,
		    1 << BQ257X0_CHARGE_OPTION_2_EN_EXTILIM_SHIFT);
	i2c_read16(I2C_PORT_POWER, 0x09, BQ25710_REG_CHARGE_OPTION_2, &reg_val);
	zassert_true(reg_val & (1 << BQ257X0_CHARGE_OPTION_2_EN_EXTILIM_SHIFT));

	cros_cbi_get_fw_config_fake.custom_fake = cbi_sb_redriver;
	rauru_common_init();
	zassert_equal(rauru_cached_sub_board, RAURU_SB_REDRIVER);

	/* Verify EXTILIM bit is cleared */
	i2c_read16(I2C_PORT_POWER, 0x09, BQ25710_REG_CHARGE_OPTION_2, &reg_val);
	zassert_false(reg_val &
		      (1 << BQ257X0_CHARGE_OPTION_2_EN_EXTILIM_SHIFT));
}

ZTEST(hooks_test, test_pd_get_drp_state_in_s0)
{
	const struct gpio_dt_spec *xhci =
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done);

	/* High input indicates XHCI inited -> toggle on */
	gpio_emul_input_set(xhci->port, xhci->pin, 1);
	zassert_equal(pd_get_drp_state_in_s0(), PD_DRP_TOGGLE_ON);

	/* Low input indicates XHCI not inited -> force sink */
	gpio_emul_input_set(xhci->port, xhci->pin, 0);
	zassert_equal(pd_get_drp_state_in_s0(), PD_DRP_FORCE_SINK);
}

ZTEST(hooks_test, test_xhci_interrupt_high)
{
	enum gpio_signal sig =
		GPIO_SIGNAL(DT_NODELABEL(gpio_ap_xhci_init_done));
	const struct gpio_dt_spec *xhci =
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done);

	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role(i, PD_DRP_FORCE_SINK);
	}

	/* Set level high */
	gpio_emul_input_set(xhci->port, xhci->pin, 1);
	xhci_interrupt(sig);

	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(pd_get_dual_role(i), PD_DRP_TOGGLE_ON);
	}
}

ZTEST(hooks_test, test_xhci_interrupt_low_attached_src)
{
	enum gpio_signal sig =
		GPIO_SIGNAL(DT_NODELABEL(gpio_ap_xhci_init_done));
	const struct gpio_dt_spec *xhci =
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done);

	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role(i, PD_DRP_TOGGLE_ON);
	}

	mock_tc_is_attached_src = true;
	gpio_emul_input_set(xhci->port, xhci->pin, 0);
	xhci_interrupt(sig);

	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(pd_get_dual_role(i), PD_DRP_FORCE_SINK);
	}
}

ZTEST(hooks_test, test_xhci_interrupt_low_not_attached_src)
{
	enum gpio_signal sig =
		GPIO_SIGNAL(DT_NODELABEL(gpio_ap_xhci_init_done));
	const struct gpio_dt_spec *xhci =
		GPIO_DT_FROM_NODELABEL(gpio_ap_xhci_init_done);

	mock_tc_is_attached_src = false;
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role(i, PD_DRP_TOGGLE_ON);
	}
	gpio_emul_input_set(xhci->port, xhci->pin, 0);
	xhci_interrupt(sig);

	/* Unchanged since not attached src */
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(pd_get_dual_role(i), PD_DRP_TOGGLE_ON);
	}
}

ZTEST(hooks_test, test_fan_low_rpm)
{
	hook_notify(HOOK_CHIPSET_RESUME);
	zassert_equal(fan_set_rpm_mode_fake.call_count, 1);
	zassert_equal(fan_set_rpm_mode_fake.arg0_val, 0);
	zassert_equal(fan_set_rpm_mode_fake.arg1_val, 1);
	zassert_equal(fan_set_rpm_target_fake.call_count, 1);
	zassert_equal(fan_set_rpm_target_fake.arg0_val, 0);
	zassert_equal(fan_set_rpm_target_fake.arg1_val, 3000);
}

ZTEST(hooks_test, test_update_bq25720_input_voltage)
{
	int reg_val;

	/* Write a non-zero value to input voltage register */
	i2c_write16(I2C_PORT_POWER, 0x09, BQ25710_REG_INPUT_VOLTAGE, 0x1234);
	i2c_read16(I2C_PORT_POWER, 0x09, BQ25710_REG_INPUT_VOLTAGE, &reg_val);
	zassert_equal(reg_val, 0x1234);

	update_bq25720_input_voltage();

	/* Verify input voltage register was cleared to 0 */
	i2c_read16(I2C_PORT_POWER, 0x09, BQ25710_REG_INPUT_VOLTAGE, &reg_val);
	zassert_equal(reg_val, 0);
}

ZTEST_SUITE(hooks_test, rauru_predicate_post_main, NULL, test_hooks_reset,
	    test_hooks_reset, NULL);
