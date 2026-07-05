/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "accelgyro.h"
#include "bbram.h"
#include "charger.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accelgyro_bmi3xx.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/charger/bq257x0_regs.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"

#include <zephyr/drivers/bbram.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_ERR);

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

static bool lid_use_alt_sensor;
void motion_interrupt(enum gpio_signal signal)
{
	if (lid_use_alt_sensor) {
		bmi3xx_interrupt(signal);
	} else {
		lsm6dsm_interrupt(signal);
	}
}

static void alt_sensor_init(void)
{
	lid_use_alt_sensor = cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(ufsc_lid_sensor_bmi323)));
	motion_sensors_check_ufsc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);

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

static void board_config_init(void)
{
	uint32_t board_version;

	if (cbi_get_board_version(&board_version) != EC_SUCCESS) {
		LOG_ERR("Failed to get board version.");
		board_version = 0;
	}

	if (board_version > 0) {
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_gsc_ec_voldn_btn_odl),
			GPIO_INPUT);
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_gsc_ec_volup_btn_odl),
			GPIO_INPUT);
	}
}
DECLARE_HOOK(HOOK_INIT, board_config_init, HOOK_PRIO_DEFAULT);

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
		CPRINTS("----first active---");

		uint32_t flags = system_get_reset_flags();
		int ext_power_present = gpio_get_level(GPIO_AC_PRESENT);
		bool allow_cutoff = (flags & EC_RESET_FLAG_POWER_ON) &&
				    !(flags & EC_RESET_FLAG_HIBERNATE) &&
				    !(flags & EC_RESET_FLAG_HARD) &&
				    !(flags & EC_RESET_FLAG_SOFT) &&
				    !(flags & EC_RESET_FLAG_WATCHDOG);

		if (allow_cutoff && (!ext_power_present) &&
		    (battery_is_present() == BP_YES)) {
			CPRINTS("##test mode: cutoff %d", ext_power_present);
			board_cut_off_battery();
		}
	}
}
DECLARE_HOOK(HOOK_INIT, check_first_boot, HOOK_PRIO_DEFAULT);
