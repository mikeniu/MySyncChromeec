/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "i2c.h"
#include "charger.h"
#include "driver/charger/bq257x0_regs.h"
#include "gpio/gpio_int.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"

LOG_MODULE_REGISTER(blanc_board, LOG_LEVEL_INF);

static void bl_enable(void)
{
	if (lid_is_open() && !chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_edp_bl), true);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_edp_bl), false);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, bl_enable, HOOK_PRIO_DEFAULT);

static void set_bq25710_charge_option(void)
{
	int reg;
	int rv;

	rv = i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			BQ25710_REG_CHARGE_OPTION_0, &reg);
	if (rv == EC_SUCCESS) {
		/* if AC only, disable IDPM,
		 * because it will cause charger keep asserting PROCHOT
		 */
		if (gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_batt_temp_r)))
			reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, EN_IDPM, 0,
					   reg);
		else
			reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, EN_IDPM, 1,
					   reg);
		i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			    BQ25710_REG_CHARGE_OPTION_0, reg);
	}
}
DECLARE_DEFERRED(set_bq25710_charge_option);

void batt_pres_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&set_bq25710_charge_option_data, 0);
}

static void batt_pres_en_init(void)
{
	hook_call_deferred(&set_bq25710_charge_option_data, 0);
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_batt_pres_en));
}
DECLARE_HOOK(HOOK_INIT, batt_pres_en_init, HOOK_PRIO_DEFAULT);
