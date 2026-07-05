/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "bbram.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/charger/bq257x0_regs.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "usb_pd.h"

#include <zephyr/drivers/bbram.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/*
 * Nereid does not have a GPIO indicating whether extpower is present,
 * so detect using the charger(s).
 */
__override void board_check_extpower(void)
{
	static int last_extpower_present;
	int extpower_present = extpower_is_present();

	if (last_extpower_present ^ extpower_present)
		extpower_handle_update(extpower_present);

	last_extpower_present = extpower_present;
}

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	charge_ma = charge_ma * 98 / 100;

	charge_set_input_current_limit(charge_ma, charge_mv);
	charger_set_input_current_limit(0, charge_ma);
}

static void delay_bq25710(void)
{
	int reg;

	i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		   BQ25710_REG_CHARGE_OPTION_2, &reg);
	reg = reg & (~BIT(7));
	i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		    BQ25710_REG_CHARGE_OPTION_2, reg);
}
DECLARE_DEFERRED(delay_bq25710);

#define BQ25710_MIN_INPUT_VOLTAGE_MV 0x240
static void bq25710_min_input_voltage(void)
{
	if (extpower_is_present()) {
		i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			    BQ25710_REG_INPUT_VOLTAGE,
			    BQ25710_MIN_INPUT_VOLTAGE_MV);
		hook_call_deferred(&delay_bq25710_data, 2 * USEC_PER_SEC);
	} else {
		int reg;

		i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			   BQ25710_REG_CHARGE_OPTION_2, &reg);
		reg = reg | BIT(7);
		i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			    BQ25710_REG_CHARGE_OPTION_2, reg);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, bq25710_min_input_voltage, HOOK_PRIO_DEFAULT);

static const struct device *const bbram_dev =
	COND_CODE_1(DT_HAS_CHOSEN(cros_ec_bbram),
		    DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram)), NULL);

static void check_first_boot(void)
{
	uint32_t board_batt_keep_val = 0;
	system_bbram_read(bbram_dev, BBRAM_REGION_OFFSET(board_batt_keep),
			  BBRAM_REGION_SIZE(board_batt_keep),
			  (uint8_t *)&board_batt_keep_val);

	if (board_batt_keep_val == 0x00) {
		board_batt_keep_val = 0xA5A5A5A5;
		system_bbram_write(bbram_dev,
				   BBRAM_REGION_OFFSET(board_batt_keep),
				   BBRAM_REGION_SIZE(board_batt_keep),
				   (uint8_t *)&board_batt_keep_val);
		LOG_INF("----first active\n");

		uint32_t flags = system_get_reset_flags();
		int ext_power_present = gpio_get_level(GPIO_AC_PRESENT);
		bool allow_cutoff = (flags & EC_RESET_FLAG_POWER_ON) &&
				    !(flags & EC_RESET_FLAG_HIBERNATE) &&
				    !(flags & EC_RESET_FLAG_HARD) &&
				    !(flags & EC_RESET_FLAG_SOFT) &&
				    !(flags & EC_RESET_FLAG_WATCHDOG);

		if (allow_cutoff && (!ext_power_present) &&
		    (battery_is_present() == BP_YES)) {
			LOG_INF("##test mode: cutoff %d", ext_power_present);
			board_cut_off_battery();
		}
	}
}
DECLARE_HOOK(HOOK_INIT, check_first_boot, HOOK_PRIO_DEFAULT);
