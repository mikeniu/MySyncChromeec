/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Quandiso hardware configuration */
#include "charger.h"
#include "driver/charger/bq257x0_regs.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usb_pd.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <ap_power/ap_power.h>

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
AP_POWER_EVENT_CALLBACK_DEFINE(pen_detect_change, AP_POWER_STARTUP,
			       AP_POWER_SHUTDOWN);

#define BQ25710_MIN_INPUT_VOLTAGE_MV 0x240
static void bq25710_min_input_voltage(void)
{
	i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		    BQ25710_REG_INPUT_VOLTAGE, BQ25710_MIN_INPUT_VOLTAGE_MV);
}
DECLARE_HOOK(HOOK_INIT, bq25710_min_input_voltage, HOOK_PRIO_DEFAULT);
