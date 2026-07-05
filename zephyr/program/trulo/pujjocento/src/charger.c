/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bbram.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(charger_settings, LOG_LEVEL_INF);

#ifdef CONFIG_PLATFORM_EC_CHARGER_BQ25720
#include "driver/charger/bq257x0_regs.h"
#endif

#ifdef CONFIG_PLATFORM_EC_CHARGER_BQ25720
void update_bq25720_input_voltage(void)
{
	/* b:397587463 set input voltage to 3.2V to prevent charger entering
	 * VINDPM mode */
	i2c_write16(chg_chips[CHARGER_SOLO].i2c_port,
		    chg_chips[CHARGER_SOLO].i2c_addr_flags,
		    BQ25710_REG_INPUT_VOLTAGE, 0);
}
DECLARE_HOOK(HOOK_AC_CHANGE, update_bq25720_input_voltage, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, update_bq25720_input_voltage, HOOK_PRIO_DEFAULT);
#endif

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
			    GPIO_DT_FROM_NODELABEL(gpio_ec_batt_pres_odl)))
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
