/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "driver/retimer/ps8802.h"
#include "driver/usb_mux/anx3443.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_tusb1064.h"
#include "hooks.h"
#include "power.h"
#include "test_state.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

void tusb1064_emul_reset_regs(const struct emul *emul);

#define PS8802_P0_NODE DT_NODELABEL(ps8802_mux_0)
#define PS8802_P1_NODE DT_NODELABEL(ps8802_p1_emul)
#define PS8802_P2_NODE DT_NODELABEL(ps8802_p2_emul)
#define ANX3443_NODE DT_NODELABEL(anx3443_mux_1)

static const struct emul *ps8802_p0_emul = EMUL_DT_GET(PS8802_P0_NODE);
static const struct emul *ps8802_p1_emul = EMUL_DT_GET(PS8802_P1_NODE);
static const struct emul *ps8802_p2_emul = EMUL_DT_GET(PS8802_P2_NODE);
static const struct emul *anx3443_emul = EMUL_DT_GET(ANX3443_NODE);

static const struct usb_mux test_ps8802_mux = {
	.usb_port = 0,
	.i2c_port = I2C_PORT_USB_C0,
	.i2c_addr_flags = 0x08,
	.driver = &ps8802_usb_mux_driver,
};

static const struct usb_mux test_anx3443_mux = {
	.usb_port = 1,
	.i2c_port = I2C_PORT_USB_C1,
	.i2c_addr_flags = 0x10,
	.driver = &anx3443_usb_mux_driver,
};

static const struct usb_mux *orig_mux0;
static const struct usb_mux *orig_mux1;

static struct i2c_common_emul_data *get_ps8802_p0_common_data(void)
{
	return (struct i2c_common_emul_data *)ps8802_p0_emul->data;
}

static struct i2c_common_emul_data *get_ps8802_p1_common_data(void)
{
	return (struct i2c_common_emul_data *)ps8802_p1_emul->data;
}

static struct i2c_common_emul_data *get_ps8802_p2_common_data(void)
{
	return (struct i2c_common_emul_data *)ps8802_p2_emul->data;
}

static struct i2c_common_emul_data *get_anx3443_common_data(void)
{
	return (struct i2c_common_emul_data *)anx3443_emul->data;
}

static void test_retimer_mux_before(void *data)
{
	ARG_UNUSED(data);

	orig_mux0 = usb_muxes[0].mux;
	orig_mux1 = usb_muxes[1].mux;
	usb_muxes[0].mux = &test_ps8802_mux;
	usb_muxes[1].mux = &test_anx3443_mux;

	/* Reset emulators */
	tusb1064_emul_reset_regs(ps8802_p0_emul);
	tusb1064_emul_reset_regs(ps8802_p1_emul);
	tusb1064_emul_reset_regs(ps8802_p2_emul);
	tusb1064_emul_reset_regs(anx3443_emul);

	i2c_common_emul_set_read_fail_reg(get_ps8802_p0_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(get_ps8802_p0_common_data(),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_fail_auto_clear(get_ps8802_p0_common_data(), false);

	i2c_common_emul_set_read_fail_reg(get_ps8802_p1_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(get_ps8802_p1_common_data(),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_fail_auto_clear(get_ps8802_p1_common_data(), false);

	i2c_common_emul_set_read_fail_reg(get_ps8802_p2_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(get_ps8802_p2_common_data(),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_fail_auto_clear(get_ps8802_p2_common_data(), false);

	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(get_anx3443_common_data(),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_fail_auto_clear(get_anx3443_common_data(), false);

	/* Default power state to S0 */
	power_set_state(POWER_S0);
}

static void test_retimer_mux_after(void *data)
{
	ARG_UNUSED(data);

	usb_muxes[0].mux = orig_mux0;
	usb_muxes[1].mux = orig_mux1;
}

/* ========================================================================= */
/* PS8802 Retimer Unit Tests                                                 */
/* ========================================================================= */

ZTEST(retimer_mux_test, test_ps8802_i2c_access_helpers)
{
	const struct usb_mux *me = usb_muxes[0].mux;
	int val = 0;
	int rv;

	/* 1. Test ps8802_i2c_write and ps8802_i2c_read */
	rv = ps8802_i2c_write(me, PS8802_REG_PAGE2, PS8802_REG2_MODE, 0x55);
	zassert_ok(rv);
	zassert_ok(
		ps8802_i2c_read(me, PS8802_REG_PAGE2, PS8802_REG2_MODE, &val));
	zassert_equal(val, 0x55);

	/* 2. Test ps8802_i2c_write16 */
	zassert_ok(ps8802_i2c_write16(me, PS8802_REG_PAGE2,
				      PS8802_REG2_USB_CEQ_LEVEL, 0x1234));
	zassert_equal(tusb1064_emul_peek_reg(ps8802_p2_emul,
					     PS8802_REG2_USB_CEQ_LEVEL),
		      0x34);
	zassert_equal(tusb1064_emul_peek_reg(ps8802_p2_emul,
					     PS8802_REG2_USB_CEQ_LEVEL + 1),
		      0x12);

	/* 3. Test ps8802_i2c_field_update8 */
	zassert_ok(
		ps8802_i2c_write(me, PS8802_REG_PAGE2, PS8802_REG2_MODE, 0xF0));
	zassert_ok(ps8802_i2c_field_update8(me, PS8802_REG_PAGE2,
					    PS8802_REG2_MODE, 0x0F, 0x0A));
	zassert_ok(
		ps8802_i2c_read(me, PS8802_REG_PAGE2, PS8802_REG2_MODE, &val));
	zassert_equal(val, 0xFA);

	/* 4. Test ps8802_i2c_field_update16 */
	zassert_ok(ps8802_i2c_write16(me, PS8802_REG_PAGE2,
				      PS8802_REG2_USB_CEQ_LEVEL, 0xFF00));
	zassert_ok(ps8802_i2c_field_update16(me, PS8802_REG_PAGE2,
					     PS8802_REG2_USB_CEQ_LEVEL, 0x00FF,
					     0x00AA));
	zassert_equal(tusb1064_emul_peek_reg(ps8802_p2_emul,
					     PS8802_REG2_USB_CEQ_LEVEL),
		      0xAA);
	zassert_equal(tusb1064_emul_peek_reg(ps8802_p2_emul,
					     PS8802_REG2_USB_CEQ_LEVEL + 1),
		      0xFF);

	/* 5. Test ps8802_chg_i2c_addr */
	zassert_ok(ps8802_chg_i2c_addr(me->i2c_port));
	zassert_equal(tusb1064_emul_peek_reg(ps8802_p2_emul, PS8802_ADDR_CFG),
		      PS8802_I2C_ADDR_FLAGS_ALT);
}

ZTEST(retimer_mux_test, test_ps8802_wake)
{
	const struct usb_mux *me = usb_muxes[0].mux;

	/* 1. Normal wake: immediate success */
	zassert_ok(ps8802_i2c_wake(me));

	/* 2. Standby wake: first read fails, second succeeds */
	i2c_common_emul_set_fail_auto_clear(get_ps8802_p2_common_data(), true);
	i2c_common_emul_set_read_fail_reg(get_ps8802_p2_common_data(),
					  PS8802_REG2_MODE);
	zassert_ok(ps8802_i2c_wake(me));

	/* 3. Wake timeout: both reads fail */
	i2c_common_emul_set_fail_auto_clear(get_ps8802_p2_common_data(), false);
	i2c_common_emul_set_read_fail_reg(get_ps8802_p2_common_data(),
					  PS8802_REG2_MODE);
	zassert_not_equal(ps8802_i2c_wake(me), EC_SUCCESS);
	i2c_common_emul_set_read_fail_reg(get_ps8802_p2_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST(retimer_mux_test, test_ps8802_init_and_low_power_mode)
{
	const struct usb_mux *me = usb_muxes[0].mux;

	/* 1. Init calls enter_low_power_mode and succeeds */
	zassert_ok(ps8802_usb_mux_driver.init(me));
	zassert_equal(tusb1064_emul_peek_reg(ps8802_p2_emul, PS8802_REG2_MODE),
		      PS8802_MODE_STANDBY_MODE);

	/* 2. Low power mode failure path */
	i2c_common_emul_set_write_fail_reg(get_ps8802_p2_common_data(),
					   PS8802_REG2_MODE);
	zassert_not_equal(ps8802_usb_mux_driver.enter_low_power_mode(me),
			  EC_SUCCESS);
	i2c_common_emul_set_write_fail_reg(get_ps8802_p2_common_data(),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST(retimer_mux_test, test_ps8802_set_mux)
{
	const struct usb_mux *me = usb_muxes[0].mux;
	bool ack_required = true;
	int reg_val;

	/* 1. HARD_OFF with NONE -> EC_SUCCESS */
	power_set_state(POWER_G3);
	zassert_ok(
		ps8802_usb_mux_driver.set(me, USB_PD_MUX_NONE, &ack_required));
	zassert_false(ack_required);

	/* 2. HARD_OFF with SAFE_MODE -> treated as NONE -> EC_SUCCESS */
	zassert_ok(ps8802_usb_mux_driver.set(me, USB_PD_MUX_SAFE_MODE,
					     &ack_required));

	/* 3. HARD_OFF with non-NONE -> EC_ERROR_NOT_POWERED */
	zassert_equal(ps8802_usb_mux_driver.set(me, USB_PD_MUX_USB_ENABLED,
						&ack_required),
		      EC_ERROR_NOT_POWERED);

	power_set_state(POWER_S0);

	/* 4. Wake failure */
	i2c_common_emul_set_read_fail_reg(get_ps8802_p2_common_data(),
					  PS8802_REG2_MODE);
	zassert_not_equal(ps8802_usb_mux_driver.set(me, USB_PD_MUX_USB_ENABLED,
						    &ack_required),
			  EC_SUCCESS);

	i2c_common_emul_set_read_fail_reg(get_ps8802_p2_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* 5. Set USB */
	zassert_ok(ps8802_usb_mux_driver.set(me, USB_PD_MUX_USB_ENABLED,
					     &ack_required));
	reg_val = tusb1064_emul_peek_reg(ps8802_p2_emul, PS8802_REG2_MODE);
	zassert_true(reg_val & PS8802_MODE_USB_ENABLE);
	zassert_false(reg_val & PS8802_MODE_DP_ENABLE);
	zassert_false(reg_val & PS8802_MODE_FLIP_ENABLE);

	/* 6. Set DP + FLIP */
	zassert_ok(ps8802_usb_mux_driver.set(
		me, USB_PD_MUX_DP_ENABLED | USB_PD_MUX_POLARITY_INVERTED,
		&ack_required));
	reg_val = tusb1064_emul_peek_reg(ps8802_p2_emul, PS8802_REG2_MODE);
	zassert_false(reg_val & PS8802_MODE_USB_ENABLE);
	zassert_true(reg_val & PS8802_MODE_DP_ENABLE);
	zassert_true(reg_val & PS8802_MODE_IN_HPD_ENABLE);
	zassert_true(reg_val & PS8802_MODE_FLIP_ENABLE);

	/* 7. Set USB + DP */
	zassert_ok(ps8802_usb_mux_driver.set(
		me, USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED,
		&ack_required));
	reg_val = tusb1064_emul_peek_reg(ps8802_p2_emul, PS8802_REG2_MODE);
	zassert_true(reg_val & PS8802_MODE_USB_ENABLE);
	zassert_true(reg_val & PS8802_MODE_DP_ENABLE);
}

ZTEST(retimer_mux_test, test_ps8802_get_mux)
{
	const struct usb_mux *me = usb_muxes[0].mux;
	mux_state_t state = 0xFF;

	/* 1. HARD_OFF -> returns EC_ERROR_NOT_POWERED and sets state to NONE */
	power_set_state(POWER_G3);
	zassert_equal(ps8802_usb_mux_driver.get(me, &state),
		      EC_ERROR_NOT_POWERED);
	zassert_equal(state, USB_PD_MUX_NONE);

	power_set_state(POWER_S0);

	/* 2. Wake failure */
	i2c_common_emul_set_read_fail_reg(get_ps8802_p2_common_data(),
					  PS8802_REG2_MODE);
	zassert_not_equal(ps8802_usb_mux_driver.get(me, &state), EC_SUCCESS);

	i2c_common_emul_set_read_fail_reg(get_ps8802_p2_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* 3. Normal get: USB + DP + FLIP */
	zassert_ok(ps8802_i2c_write(me, PS8802_REG_PAGE2, PS8802_REG2_MODE,
				    PS8802_MODE_USB_ENABLE |
					    PS8802_MODE_DP_ENABLE |
					    PS8802_MODE_FLIP_ENABLE));
	zassert_ok(ps8802_usb_mux_driver.get(me, &state));
	zassert_equal(state, USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED |
				     USB_PD_MUX_POLARITY_INVERTED);

	/* 4. Normal get: NONE */
	zassert_ok(
		ps8802_i2c_write(me, PS8802_REG_PAGE2, PS8802_REG2_MODE, 0x00));
	zassert_ok(ps8802_usb_mux_driver.get(me, &state));
	zassert_equal(state, USB_PD_MUX_NONE);
}

/* ========================================================================= */
/* ANX3443 Retimer Unit Tests                                                */
/* ========================================================================= */

ZTEST(retimer_mux_test, test_anx3443_set_mux)
{
	const struct usb_mux *me = usb_muxes[1].mux;
	bool ack_required = true;
	int reg_val;

	/* 1. Disable both DP and USB (or NONE) -> calls anx3443_power_off */
	zassert_ok(anx3443_usb_mux_driver.set(me, USB_PD_MUX_USB_ENABLED,
					      &ack_required));
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX3443_REG_POWER_CNTRL,
		   0);
	zassert_ok(
		anx3443_usb_mux_driver.set(me, USB_PD_MUX_NONE, &ack_required));
	zassert_false(ack_required);
	zassert_equal(tusb1064_emul_peek_reg(anx3443_emul,
					     ANX3443_REG_POWER_CNTRL),
		      ANX3443_POWER_CNTRL_OFF);

	/* 2. SAFE_MODE treated as NONE */
	zassert_ok(anx3443_usb_mux_driver.set(me, USB_PD_MUX_SAFE_MODE,
					      &ack_required));

	/* 3. In suspend state (S3), non-NONE is deferred -> EC_SUCCESS */
	power_set_state(POWER_S3);
	zassert_ok(anx3443_usb_mux_driver.set(me, USB_PD_MUX_USB_ENABLED,
					      &ack_required));

	power_set_state(POWER_S0);

	/* 4. Wake up timeout failure */
	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(), 0x00);
	zassert_equal(anx3443_usb_mux_driver.set(me, USB_PD_MUX_USB_ENABLED,
						 &ack_required),
		      EC_ERROR_TIMEOUT);

	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* 5. Set USB enabled */
	zassert_ok(anx3443_usb_mux_driver.set(me, USB_PD_MUX_USB_ENABLED,
					      &ack_required));
	reg_val =
		tusb1064_emul_peek_reg(anx3443_emul, ANX3443_REG_ULP_CFG_MODE);
	zassert_true(reg_val & ANX3443_ULP_CFG_MODE_EN);
	zassert_true(reg_val & ANX3443_ULP_CFG_MODE_USB_EN);
	zassert_false(reg_val & ANX3443_ULP_CFG_MODE_DP_EN);
	zassert_false(reg_val & ANX3443_ULP_CFG_MODE_FLIP);

	/* 6. Set DP enabled and Polarity inverted */
	zassert_ok(anx3443_usb_mux_driver.set(
		me, USB_PD_MUX_DP_ENABLED | USB_PD_MUX_POLARITY_INVERTED,
		&ack_required));
	reg_val =
		tusb1064_emul_peek_reg(anx3443_emul, ANX3443_REG_ULP_CFG_MODE);
	zassert_true(reg_val & ANX3443_ULP_CFG_MODE_EN);
	zassert_false(reg_val & ANX3443_ULP_CFG_MODE_USB_EN);
	zassert_true(reg_val & ANX3443_ULP_CFG_MODE_DP_EN);
	zassert_true(reg_val & ANX3443_ULP_CFG_MODE_FLIP);

	/* 7. Write failure on setting ULP_CFG_MODE */
	i2c_common_emul_set_write_fail_reg(get_anx3443_common_data(),
					   ANX3443_REG_ULP_CFG_MODE);
	zassert_not_equal(anx3443_usb_mux_driver.set(me, USB_PD_MUX_USB_ENABLED,
						     &ack_required),
			  EC_SUCCESS);
	i2c_common_emul_set_write_fail_reg(get_anx3443_common_data(),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST(retimer_mux_test, test_anx3443_get_mux)
{
	const struct usb_mux *me = usb_muxes[1].mux;
	mux_state_t state = 0xFF;

	/* 1. HARD_OFF -> returns USB_PD_MUX_NONE */
	power_set_state(POWER_G3);
	zassert_equal(anx3443_usb_mux_driver.get(me, &state), USB_PD_MUX_NONE);

	power_set_state(POWER_S0);

	/* 2. Wake failure */
	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(), 0x00);
	zassert_equal(anx3443_usb_mux_driver.get(me, &state), EC_ERROR_TIMEOUT);

	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* 3. Read failure on register read */
	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(),
					  ANX3443_REG_ULP_CFG_MODE);
	zassert_not_equal(anx3443_usb_mux_driver.get(me, &state), EC_SUCCESS);

	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* 4. Normal get: USB + DP + FLIP */
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX3443_REG_ULP_CFG_MODE,
		   ANX3443_ULP_CFG_MODE_USB_EN | ANX3443_ULP_CFG_MODE_DP_EN |
			   ANX3443_ULP_CFG_MODE_FLIP);
	zassert_ok(anx3443_usb_mux_driver.get(me, &state));
	zassert_equal(state, USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED |
				     USB_PD_MUX_POLARITY_INVERTED);

	/* 5. Normal get: 0 */
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX3443_REG_ULP_CFG_MODE,
		   0);
	zassert_ok(anx3443_usb_mux_driver.get(me, &state));
	zassert_equal(state, 0);
}

ZTEST(retimer_mux_test, test_anx3443_init)
{
	const struct usb_mux *me = usb_muxes[1].mux;

	/* 1. Normal init succeeds and defaults to USB */
	zassert_ok(anx3443_usb_mux_driver.init(me));
	zassert_true(
		tusb1064_emul_peek_reg(anx3443_emul, ANX3443_REG_ULP_CFG_MODE) &
		ANX3443_ULP_CFG_MODE_USB_EN);

	/* 2. Wake up failure in init */
	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(), 0x00);
	zassert_equal(anx3443_usb_mux_driver.init(me), EC_ERROR_TIMEOUT);
	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST(retimer_mux_test, test_anx3443_suspend_and_resume)
{
	const struct usb_mux *me = usb_muxes[1].mux;
	bool ack_required;

	/* Initialize and set to USB mode */
	zassert_ok(anx3443_usb_mux_driver.init(me));

	/* Case A: Port is NOT USB2-only (UP_EN_RTERM_ST is set) -> not powered
	 * off on suspend */
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX3443_REG_POWER_CNTRL,
		   0);
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX3443_REG_USB_STATUS,
		   ANX3443_UP_EN_RTERM_ST);
	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_not_equal(tusb1064_emul_peek_reg(anx3443_emul,
						 ANX3443_REG_POWER_CNTRL),
			  ANX3443_POWER_CNTRL_OFF);

	/* Case B: Port read fails -> not USB2-only */
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX3443_REG_POWER_CNTRL,
		   0);
	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(),
					  ANX3443_REG_USB_STATUS);
	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_not_equal(tusb1064_emul_peek_reg(anx3443_emul,
						 ANX3443_REG_POWER_CNTRL),
			  ANX3443_POWER_CNTRL_OFF);
	i2c_common_emul_set_read_fail_reg(get_anx3443_common_data(),
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Case C: Port is not in USB mode (e.g. NONE) -> port_is_usb2_only is
	 * false */
	zassert_ok(
		anx3443_usb_mux_driver.set(me, USB_PD_MUX_NONE, &ack_required));
	hook_notify(HOOK_CHIPSET_SUSPEND);

	/* Case D: Port is USB2-only (USB enabled, UP_EN_RTERM_ST is NOT set) ->
	 * powers off */
	zassert_ok(anx3443_usb_mux_driver.set(me, USB_PD_MUX_USB_ENABLED,
					      &ack_required));
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX3443_REG_POWER_CNTRL,
		   0);
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX3443_REG_USB_STATUS,
		   0x00);
	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_equal(tusb1064_emul_peek_reg(anx3443_emul,
					     ANX3443_REG_POWER_CNTRL),
		      ANX3443_POWER_CNTRL_OFF);

	/* Calling power_off again when already off hits early return */
	hook_notify(HOOK_CHIPSET_SUSPEND);

	/* Resume restores the saved mux state (USB_PD_MUX_USB_ENABLED) */
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX3443_REG_ULP_CFG_MODE,
		   0);
	hook_notify(HOOK_CHIPSET_RESUME);
	zassert_true(
		tusb1064_emul_peek_reg(anx3443_emul, ANX3443_REG_ULP_CFG_MODE) &
		ANX3443_ULP_CFG_MODE_USB_EN);
}

ZTEST_SUITE(retimer_mux_test, rauru_predicate_post_main, NULL,
	    test_retimer_mux_before, test_retimer_mux_after, NULL);
