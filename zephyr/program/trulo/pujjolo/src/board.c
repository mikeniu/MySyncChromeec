/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Quandiso hardware configuration */
#include "bbram.h"
#include "charge_state.h"
#include "charger.h"
#include "driver/charger/bq257x0_regs.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "power_button.h"
#include "system.h"
#include "usb_pd.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(trulo, LOG_LEVEL_INF);

void pen_detect_interrupt(enum gpio_signal s)
{
	const struct gpio_dt_spec *const pen_detect_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl);
	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x);
	int pen_detect = !gpio_pin_get_dt(pen_detect_gpio);

	gpio_pin_set_dt(pen_power_gpio, pen_detect);
}

test_export_static void pen_detect_change(struct ap_power_ev_callback *cb,
					  struct ap_power_ev_data data)
{
	const struct gpio_dt_spec *const pen_detect_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl);
	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x);
	const struct gpio_int_config *const pen_detect_int =
		GPIO_INT_FROM_NODELABEL(int_pen_det_l);

	switch (data.event) {
	case AP_POWER_STARTUP:
		/* Enable Pen Detect interrupt */
		gpio_enable_dt_interrupt(pen_detect_int);
		/*
		 * Make sure pen detection is triggered or not when AP power on
		 */
		if (!gpio_pin_get_dt(pen_detect_gpio))
			gpio_pin_set_dt(pen_power_gpio, 1);
		break;
	case AP_POWER_SHUTDOWN:
		/*
		 * Disable pen detect INT and turn off pen power when AP
		 * shutdown
		 */
		gpio_disable_dt_interrupt(pen_detect_int);
		gpio_pin_set_dt(pen_power_gpio, 0);
		break;
	default:
		break;
	}
}

static void pen_init(void)
{
	static struct ap_power_ev_callback cb;

	ap_power_ev_init_callback(&cb, pen_detect_change,
				  AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);
}
DECLARE_HOOK(HOOK_INIT, pen_init, HOOK_PRIO_INIT_I2C);

#define BQ25710_MIN_INPUT_VOLTAGE_MV 0x240
static void bq25710_min_input_voltage(void)
{
	i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		    BQ25710_REG_INPUT_VOLTAGE, BQ25710_MIN_INPUT_VOLTAGE_MV);
}
DECLARE_HOOK(HOOK_INIT, bq25710_min_input_voltage, HOOK_PRIO_DEFAULT);

static const struct device *const bbram_dev =
	COND_CODE_1(DT_HAS_CHOSEN(cros_ec_bbram),
		    DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram)), NULL);

static int system_get_battkeep(uint32_t *value)
{
	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't binding");
		return -EC_ERROR_INVAL;
	}

	if (system_bbram_read(bbram_dev, BBRAM_REGION_OFFSET(board_batt_keep),
			      BBRAM_REGION_SIZE(board_batt_keep),
			      (uint8_t *)value)) {
		return -EC_ERROR_INVAL;
	}

	return 0;
}

static int system_set_battkeep(uint32_t value)
{
	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't binding");
		return -EC_ERROR_INVAL;
	}

	return system_bbram_write(bbram_dev,
				  BBRAM_REGION_OFFSET(board_batt_keep),
				  BBRAM_REGION_SIZE(board_batt_keep),
				  (uint8_t *)&value);
}

#ifdef CONFIG_BOARD_PREVENT_POWERON_CUSTOM
__override bool board_prevent_power_on_custom(void)
{
	uint32_t board_batt_keep_val;

	system_get_battkeep(&board_batt_keep_val);
	if ((board_batt_keep_val == 0) || (board_batt_keep_val == 0xA5A5A5A5)) {
		if ((battery_is_present() == BP_YES) &&
		    !extpower_is_present()) {
			return true;
		}
		board_batt_keep_val = 0xA2A2A2A2;
		system_set_battkeep(board_batt_keep_val);
		return false;
	} else {
		return false;
	}
}

static void ac_change_clear_reset_cause(void)
{
	uint32_t board_batt_keep_val;
	system_get_battkeep(&board_batt_keep_val);
	if (((board_batt_keep_val == 0) ||
	     (board_batt_keep_val == 0xA5A5A5A5)) &&
	    extpower_is_present()) {
		board_batt_keep_val = 0xA1A1A1A1;
		system_set_battkeep(board_batt_keep_val);
		system_reset(SYSTEM_RESET_HARD);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, ac_change_clear_reset_cause, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_BOARD_PREVENT_POWERON_CUSTOM */

static void check_first_boot(void)
{
	uint32_t board_batt_keep_val = 0;
	system_get_battkeep(&board_batt_keep_val);

	if (board_batt_keep_val == 0x00) {
		board_batt_keep_val = 0xA5A5A5A5;
		system_set_battkeep(board_batt_keep_val);
	}
}
DECLARE_HOOK(HOOK_INIT, check_first_boot, HOOK_PRIO_DEFAULT);
