/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Quandiso hardware configuration */
#include "charge_state.h"
#include "charger.h"
#include "extpower.h"
#include "driver/charger/bq257x0_regs.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
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

#ifdef CONFIG_BOARD_PREVENT_POWERON_CUSTOM
__override bool board_prevent_power_on_custom(void)
{
	uint32_t system_flags = chip_read_reset_flags(); /* system reset flag */

	if (system_flags & ~(EC_RESET_FLAG_POWER_ON | EC_RESET_FLAG_INITIAL_PWR)) {
		LOG_ERR("board_prevent_power_on_custom false 0x%X", system_flags);
		return false;
	} else {
		LOG_ERR("board_prevent_power_on_custom true 0x%X", system_flags);
		return true;
	}
}

static void ac_change_clear_reset_cause(void)
{
	LOG_ERR("ac_change_clear_reset_cause original: 0x%X", chip_read_reset_flags());
	if (extpower_is_present()) {
		LOG_ERR("ac_change_clear_reset_cause");
		system_clear_reset_flags(EC_RESET_FLAG_POWER_ON | EC_RESET_FLAG_INITIAL_PWR);
	}
	LOG_ERR("ac_change_clear_reset_cause : 0x%X", chip_read_reset_flags());
}
DECLARE_HOOK(HOOK_AC_CHANGE, ac_change_clear_reset_cause, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_BOARD_PREVENT_POWERON_CUSTOM */