/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Renesas (Intersil) ISL-95522 battery charger driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "isl95522.h"
#include "printf.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHARGER, "ISL95522 " format, ##args)

static int learn_mode;

/* Mutex for CONTROL1 register, that can be updated from multiple tasks. */
static K_MUTEX_DEFINE(control1_mutex_isl95522);

/* Mutex for CONTROL2 register, that can be updated from multiple tasks. */
static K_MUTEX_DEFINE(control2_mutex_isl95522);

/* Charger parameters */
static const struct charger_info isl95522_charger_info = {
	.name = CHARGER_NAME,
	.voltage_max = CHARGE_V_MAX,
	.voltage_min = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max = CHARGE_I_MAX,
	.current_min = BC_REG_TO_CURRENT(CHARGE_I_MIN),
	.current_step = BC_REG_TO_CURRENT(CHARGE_I_STEP),
	.input_current_max = INPUT_I_MAX,
	.input_current_min = AC_REG_TO_CURRENT(INPUT_I_MIN),
	.input_current_step = AC_REG_TO_CURRENT(INPUT_I_STEP),
};

static enum ec_error_list isl95522_discharge_on_ac(int chgnum, int enable);
static enum ec_error_list isl95522_discharge_on_ac_unsafe(int chgnum,
							  int enable);
static enum ec_error_list isl95522_discharge_on_ac_weak_disable(int chgnum);

static enum ec_error_list isl95522_read(int chgnum, int offset, int *value)
{
	int rv = i2c_read16(chg_chips[chgnum].i2c_port,
			    chg_chips[chgnum].i2c_addr_flags, offset, value);
	if (rv)
		CPRINTS("%s failed (%d)", __func__, rv);

	return rv;
}

static enum ec_error_list isl95522_write(int chgnum, int offset, int value)
{
	int rv = i2c_write16(chg_chips[chgnum].i2c_port,
			     chg_chips[chgnum].i2c_addr_flags, offset, value);
	if (rv)
		CPRINTS("%s failed (%d)", __func__, rv);

	return rv;
}

static enum ec_error_list isl95522_update(int chgnum, int offset, uint16_t mask,
					  enum mask_update_action action)
{
	int rv = i2c_update16(chg_chips[chgnum].i2c_port,
			      chg_chips[chgnum].i2c_addr_flags, offset, mask,
			      action);
	if (rv)
		CPRINTS("%s failed (%d)", __func__, rv);

	return rv;
}

/* chip specific interfaces */

/*****************************************************************************/
/* Charger interfaces */
static enum ec_error_list isl95522_set_input_current_limit(int chgnum,
							   int input_current)
{
	int rv;
	uint16_t reg = AC_CURRENT_TO_REG(input_current);

	rv = isl95522_write(chgnum, ISL95522_REG_ADAPTER_CUR_LIMIT1, reg);
	if (rv)
		return rv;

	return isl95522_write(chgnum, ISL95522_REG_ADAPTER_CUR_LIMIT2, reg);
}

static enum ec_error_list isl95522_get_input_current_limit(int chgnum,
							   int *input_current)
{
	int rv;

	rv = isl95522_read(chgnum, ISL95522_REG_ADAPTER_CUR_LIMIT1,
			   input_current);
	if (rv)
		return rv;

	*input_current = AC_REG_TO_CURRENT(*input_current);
	return EC_SUCCESS;
}

static enum ec_error_list isl95522_manufacturer_id(int chgnum, int *id)
{
	return isl95522_read(chgnum, ISL95522_REG_MANUFACTURER_ID, id);
}

static enum ec_error_list isl95522_device_id(int chgnum, int *id)
{
	return isl95522_read(chgnum, ISL95522_REG_DEVICE_ID, id);
}

static enum ec_error_list isl95522_set_frequency(int chgnum, int freq_khz)
{
	int rv;
	int reg;
	int freq;

	mutex_lock(&control2_mutex_isl95522);
	rv = isl95522_read(chgnum, ISL95522_REG_CONTROL2, &reg);
	if (rv) {
		CPRINTS("Could not read CONTROL2. (rv=%d)", rv);
		goto error;
	}

	if (freq_khz >= 869)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_942KHZ;
	else if (freq_khz >= 744)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_796KHZ;
	else if (freq_khz >= 670)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_693KHZ;
	else if (freq_khz >= 630)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_644KHZ;
	else if (freq_khz >= 595)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_610KHZ;
	else if (freq_khz >= 560)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_583KHZ;
	else if (freq_khz >= 520)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_533KHZ;
	else if (freq_khz >= 489)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_491KHZ;
	else if (freq_khz >= 470)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_487KHZ;
	else if (freq_khz >= 445)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_453KHZ;
	else if (freq_khz >= 415)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_427KHZ;
	else if (freq_khz >= 398)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_400KHZ;
	else if (freq_khz >= 385)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_396KHZ;
	else if (freq_khz >= 365)
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_375KHZ;
	else
		freq = ISL95522_CONTROL2_SWITCHING_FREQ_356KHZ;

	reg &= ~ISL95522_CONTROL2_SWITCHING_FREQ_MASK;
	reg |= (freq << ISL95522_CONTROL2_SWITCHING_FREQ_SHIFT);
	rv = isl95522_write(chgnum, ISL95522_REG_CONTROL2, reg);
	if (rv) {
		CPRINTS("Could not write CONTROL2. (rv=%d)", rv);
		goto error;
	}

error:
	mutex_unlock(&control2_mutex_isl95522);

	return rv;
}

static enum ec_error_list isl95522_get_option(int chgnum, int *option)
{
	int rv;
	uint32_t controls;
	int reg;

	rv = isl95522_read(chgnum, ISL95522_REG_CONTROL1, &reg);
	if (rv)
		return rv;

	controls = reg;
	rv = isl95522_read(chgnum, ISL95522_REG_CONTROL2, &reg);
	if (rv)
		return rv;

	controls |= reg << 16;
	*option = controls;
	return EC_SUCCESS;
}

static enum ec_error_list isl95522_set_option(int chgnum, int option)
{
	int rv;

	rv = isl95522_write(chgnum, ISL95522_REG_CONTROL1, option & 0xFFFF);
	if (rv)
		return rv;

	return isl95522_write(chgnum, ISL95522_REG_CONTROL2,
			      (option >> 16) & 0xFFFF);
}

static const struct charger_info *isl95522_get_info(int chgnum)
{
	return &isl95522_charger_info;
}

static enum ec_error_list isl95522_get_status(int chgnum, int *status)
{
	int rv;
	int reg;

	/* Level 2 charger */
	*status = CHARGER_LEVEL_2;

	/* Charge inhibit status */
	rv = isl95522_read(chgnum, ISL95522_REG_CONTROL1, &reg);
	if (rv)
		return rv;
	if (!(reg & ISL95522_REG_CONTROL1_ENABLE_CHARGING))
		*status |= CHARGER_CHARGE_INHIBITED;

	/* AC present status */
	rv = isl95522_read(chgnum, ISL95522_REG_INFORMATION1, &reg);
	if (rv)
		return rv;
	if (reg & ISL95522_REG_INFORMATION1_AC_PRESENT)
		*status |= CHARGER_AC_PRESENT;

	return EC_SUCCESS;
}

static enum ec_error_list isl95522_set_mode(int chgnum, int mode)
{
	int rv;

	/*
	 * See crosbug.com/p/51196.
	 * Disable learn mode if it wasn't explicitly enabled.
	 */
	rv = isl95522_discharge_on_ac_weak_disable(chgnum);
	if (rv)
		return rv;

	/*
	 * Charger inhibit
	 */
	rv = isl95522_update(chgnum, ISL95522_REG_CONTROL1,
			     ISL95522_REG_CONTROL1_ENABLE_CHARGING,
			     !(mode & CHARGE_FLAG_INHIBIT_CHARGE) ? MASK_SET :
								    MASK_CLR);
	if (rv)
		return rv;

	return rv;
}

static enum ec_error_list isl95522_get_current(int chgnum, int *current)
{
	int rv;

	rv = isl95522_read(chgnum, ISL95522_REG_CHG_CURRENT_LIMIT, current);
	if (rv)
		return rv;

	*current = BC_REG_TO_CURRENT(*current);
	return EC_SUCCESS;
}

static enum ec_error_list isl95522_set_current(int chgnum, int current)
{
	return isl95522_write(chgnum, ISL95522_REG_CHG_CURRENT_LIMIT,
			      BC_CURRENT_TO_REG(current));
}

static enum ec_error_list isl95522_get_voltage(int chgnum, int *voltage)
{
	return isl95522_read(chgnum, ISL95522_REG_MAX_SYSTEM_VOLTAGE, voltage);
}

static enum ec_error_list isl95522_set_voltage(int chgnum, int voltage)
{
	return isl95522_write(chgnum, ISL95522_REG_MAX_SYSTEM_VOLTAGE, voltage);
}

static enum ec_error_list isl95522_post_init(int chgnum)
{
	return EC_SUCCESS;
}

/*
 * Writes to ISL95522_REG_CONTROL1, unsafe as it does not lock
 * control1_mutex_isl95522.
 */
static enum ec_error_list isl95522_discharge_on_ac_unsafe(int chgnum,
							  int enable)
{
	int rv = isl95522_update(chgnum, ISL95522_REG_CONTROL1,
				 ISL95522_REG_CONTROL1_LEARN_MODE,
				 (enable) ? MASK_SET : MASK_CLR);
	if (!rv)
		learn_mode = enable;

	return rv;
}

/* Disables discharge on ac only if it wasn't explicitly enabled. */
static enum ec_error_list isl95522_discharge_on_ac_weak_disable(int chgnum)
{
	int rv = 0;

	mutex_lock(&control1_mutex_isl95522);
	if (!learn_mode) {
		rv = isl95522_discharge_on_ac_unsafe(chgnum, 0);
	}

	mutex_unlock(&control1_mutex_isl95522);
	return rv;
}

static enum ec_error_list isl95522_discharge_on_ac(int chgnum, int enable)
{
	int rv = 0;

	mutex_lock(&control1_mutex_isl95522);
	rv = isl95522_discharge_on_ac_unsafe(chgnum, enable);
	mutex_unlock(&control1_mutex_isl95522);
	return rv;
}

int isl95522_set_ac_prochot(int chgnum, int ma)
{
	int rv;
	uint16_t reg;

	/*
	 * The register reserves bits [6:0] and bits [15:13].
	 * This routine should ensure these bits are not set before
	 * writing the register.
	 */
	if (ma > ISL95522_AC_PROCHOT_CURRENT_MAX)
		reg = AC_CURRENT_TO_REG(ISL95522_AC_PROCHOT_CURRENT_MAX);
	else if (ma < ISL95522_AC_PROCHOT_CURRENT_MIN)
		reg = AC_CURRENT_TO_REG(ISL95522_AC_PROCHOT_CURRENT_MIN);
	else
		reg = AC_CURRENT_TO_REG(ma);

	rv = isl95522_write(chgnum, ISL95522_REG_AC_PROCHOT, reg);
	if (rv)
		CPRINTS("set_ac_prochot failed (%d)", rv);

	return rv;
}

int isl95522_set_dc_prochot(int chgnum, int ma)
{
	int rv;
	uint16_t reg;

	/*
	 * The register reserves bits [6:0] and bits [15:13].
	 * This routine should ensure these bits are not set before
	 * writing the register.
	 */
	if (ma > ISL95522_DC_PROCHOT_CURRENT_MAX)
		reg = BC_CURRENT_TO_REG(ISL95522_DC_PROCHOT_CURRENT_MAX);
	else if (ma < ISL95522_DC_PROCHOT_CURRENT_MIN)
		reg = BC_CURRENT_TO_REG(ISL95522_DC_PROCHOT_CURRENT_MIN);
	else
		reg = BC_CURRENT_TO_REG(ma);

	rv = isl95522_write(chgnum, ISL95522_REG_DC_PROCHOT, reg);
	if (rv)
		CPRINTS("set_dc_prochot failed (%d)", rv);

	return rv;
}

/*****************************************************************************/
/* ISL-95522 initialization */
static void isl95522_init(int chgnum)
{
	int ctl_val, rv;
	rv = 0;

	const struct battery_info *bi = battery_get_info();

	/*
	 * Set the MaxSystemVoltage to battery maximum,
	 * 0x00=disables switching charger states
	 */
	if (isl95522_write(chgnum, ISL95522_REG_MAX_SYSTEM_VOLTAGE,
			   bi->voltage_max))
		goto init_fail;

	/*
	 * Set the MinSystemVoltage to battery minimum,
	 * 0x00=disables all battery charging
	 */
	if (isl95522_write(chgnum, ISL95522_REG_MIN_SYSTEM_VOLTAGE,
			   bi->voltage_min))
		goto init_fail;

	mutex_lock(&control1_mutex_isl95522);
	/*
	 * Set control1 registers
	 */
	ctl_val = 0;
	ctl_val |= ISL95522_REG_CONTROL1_WOCP;
	if (isl95522_write(chgnum, ISL95522_REG_CONTROL1, ctl_val))
		rv = 1;
	mutex_unlock(&control1_mutex_isl95522);
	if (rv)
		goto init_fail;

	mutex_lock(&control2_mutex_isl95522);
	/*
	 * Set control2 register
	 */
	ctl_val = 0;
	ctl_val |= ISL95522_REG_CONTROL2_TRICKLE_CHARGE;
	if (isl95522_write(chgnum, ISL95522_REG_CONTROL2, ctl_val))
		rv = 1;
	mutex_unlock(&control2_mutex_isl95522);
	if (rv)
		goto init_fail;

	/*
	 * No need to proceed with the rest of init if we sysjump'd to this
	 * image as the input current limit has already been set.
	 */
	if (system_jumped_late())
		return;

	/* Initialize the input current limit to the board's default. */
	if (isl95522_set_input_current_limit(
		    chgnum, CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT))
		goto init_fail;

	return;

init_fail:
	CPRINTS("Init failed!");
}

/*****************************************************************************/
#ifdef CONFIG_CMD_CHARGER_DUMP
static void dump_reg_range(int chgnum, int low, int high)
{
	int reg;
	int regval;
	int rv;

	cflush();
	for (reg = low; reg <= high; reg++) {
		ccprintf("[%Xh] = ", reg);
		rv = isl95522_read(chgnum, reg, &regval);
		if (!rv)
			ccprintf("0x%04x\n", regval);
		else
			ccprintf("ERR (%d)\n", rv);
		cflush();
	}
}

static void command_isl95522_dump(int chgnum)
{
	dump_reg_range(chgnum, 0x14, 0x15);
	dump_reg_range(chgnum, 0x37, 0x40);
	dump_reg_range(chgnum, 0x45, 0x48);
	dump_reg_range(chgnum, 0xFE, 0xFF);
}
#endif /* CONFIG_CMD_CHARGER_DUMP */

const struct charger_drv isl95522_drv = {
	.init = &isl95522_init,
	.post_init = &isl95522_post_init,
	.get_info = &isl95522_get_info,
	.get_status = &isl95522_get_status,
	.set_mode = &isl95522_set_mode,
	.get_current = &isl95522_get_current,
	.set_current = &isl95522_set_current,
	.get_voltage = &isl95522_get_voltage,
	.set_voltage = &isl95522_set_voltage,
	.discharge_on_ac = &isl95522_discharge_on_ac,
	.set_input_current_limit = &isl95522_set_input_current_limit,
	.get_input_current_limit = &isl95522_get_input_current_limit,
	.manufacturer_id = &isl95522_manufacturer_id,
	.device_id = &isl95522_device_id,
	.set_frequency = &isl95522_set_frequency,
	.get_option = &isl95522_get_option,
	.set_option = &isl95522_set_option,
#ifdef CONFIG_CMD_CHARGER_DUMP
	.dump_registers = &command_isl95522_dump,
#endif
};
