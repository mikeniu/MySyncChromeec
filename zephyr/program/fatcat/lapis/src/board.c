/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "charger.h"
#include "cros_board_info.h"
<<<<<<< HEAD   (1117b5a705f58372c1b06f9c08d0a2fa735f607d rex/karis: Supports higher system clock frequency of 40MHz)
||||||| BASE   (6bd8aaf22f4eaa1a1c3a483ae9f29d7a8209199a rex/karis: Supports higher system clock frequency of 40MHz)
#include "cros_cbi.h"
=======
#include "cros_cbi.h"
#include "driver/charger/isl9241.h"
#include "extpower.h"
>>>>>>> CHANGE (75a0032757ed529ab9521edd46c425f143ff2458 lapis: Set charger register on ac only state)
#include "gpio.h"
#include "hooks.h"
<<<<<<< HEAD   (1117b5a705f58372c1b06f9c08d0a2fa735f607d rex/karis: Supports higher system clock frequency of 40MHz)
||||||| BASE   (6bd8aaf22f4eaa1a1c3a483ae9f29d7a8209199a rex/karis: Supports higher system clock frequency of 40MHz)
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
=======
#include "i2c.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
>>>>>>> CHANGE (75a0032757ed529ab9521edd46c425f143ff2458 lapis: Set charger register on ac only state)
#include "lid_switch.h"

static void set_chg_reg_custom(void)
{
	charger_set_frequency(808);
}
DECLARE_HOOK(HOOK_INIT, set_chg_reg_custom, HOOK_PRIO_POST_BATTERY_INIT + 1);

static void set_chg_control3(void)
{
	int reg, rv;

	rv = i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			ISL9241_REG_CONTROL3, &reg);

	if (rv || (reg & ISL9241_CONTROL3_INPUT_CURRENT_LIMIT))
		return;

	reg |= ISL9241_CONTROL3_INPUT_CURRENT_LIMIT;
	rv = i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			 ISL9241_REG_CONTROL3, reg);

	if (rv)
		return;

	LOG_INF("Disable Input Current Limit");
}

static void restore_chg_control3(void)
{
	int reg, rv;

	rv = i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			ISL9241_REG_CONTROL3, &reg);
	if (rv || (!(reg & ISL9241_CONTROL3_INPUT_CURRENT_LIMIT)))
		return;

	reg &= ~ISL9241_CONTROL3_INPUT_CURRENT_LIMIT;
	rv = i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			 ISL9241_REG_CONTROL3, reg);

	if (rv)
		return;

	LOG_INF("Enable Input Current Limit");
}

static void detect_aconly(void)
{
	const struct batt_params *batt = charger_current_battery_params();

	if (extpower_is_present()) {
		if (batt->is_present == BP_NO) {
			set_chg_control3();
		} else {
			restore_chg_control3();
		}
	}
}
DECLARE_HOOK(HOOK_INIT, detect_aconly, HOOK_PRIO_DEFAULT + 1);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, detect_aconly, HOOK_PRIO_DEFAULT + 1);

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
