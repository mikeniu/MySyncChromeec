/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "cros_board_info.h"
#include "driver/charger/isl9241.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "lid_switch.h"

static void set_chg_reg_custom(void)
{
	charger_set_frequency(808);
}
DECLARE_HOOK(HOOK_INIT, set_chg_reg_custom, HOOK_PRIO_POST_BATTERY_INIT + 1);

static void tp_enable(void)
{
	if (lid_is_open()) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_disable), true);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_disable), false);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, tp_enable, HOOK_PRIO_DEFAULT);

static void disable_sleep_bid(void)
{
	uint32_t board_id = 0;
	/* Errors will count as board_id 0 */
	cbi_get_board_version(&board_id);

	if (board_id > 1)
		enable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
	else
		disable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
}
DECLARE_HOOK(HOOK_INIT, disable_sleep_bid, HOOK_PRIO_POST_I2C);

static void aconly_isl9241(void)
{
	int reg, rv;

	i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		   ISL9241_REG_CONTROL3, &reg);
	printk("--read 0x4C_reg=0x%02x\n", reg);
	reg = reg | BIT(5);
	i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		    ISL9241_REG_CONTROL3, reg);
	printk("--write 0x4C_reg=0x%02x\n", reg);

	// rv = charger_set_voltage(0,9000);
	// printk("--set voltage 9000mV\n");
	// if (rv != EC_SUCCESS)
	// 	printk("--set voltage fail!\n");
}

static void detect_aconly(void);
DECLARE_DEFERRED(detect_aconly);
static void detect_aconly(void)
{
	static int detect_cnt;

	if (extpower_is_present()) {
		if (battery_is_present() == BP_NO) {
			aconly_isl9241();
		} else if (detect_cnt < 3) {
			detect_cnt++;
			hook_call_deferred(&detect_aconly_data,
					   CONFIG_BATTERY_PRECHARGE_TIMEOUT *
						   USEC_PER_SEC);
		}
	}
}
DECLARE_HOOK(HOOK_INIT, detect_aconly, HOOK_PRIO_DEFAULT);
