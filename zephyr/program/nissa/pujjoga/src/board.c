/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Quandiso hardware configuration */
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "nissa_common.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

void pen_detect_interrupt(enum gpio_signal s)
{
	const struct gpio_dt_spec *const pen_detect_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl);
	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen);
	int pen_detect = !gpio_pin_get_dt(pen_detect_gpio);

	gpio_pin_set_dt(pen_power_gpio, pen_detect);
}

test_export_static void pen_detect_change(struct ap_power_ev_callback *cb,
					  struct ap_power_ev_data data)
{
	const struct gpio_dt_spec *const pen_detect_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl);
	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen);
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

#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>

/* Create a global counter to prove it happened */
volatile uint32_t suspend_count = 0;
volatile uint32_t resume_count = 0;

/* Define the callback for when the system wakes up */
static void my_pm_state_enter_hook(enum pm_state state)
{
    suspend_count++;
}

/* Define the callback for when the system wakes up */
static void my_pm_state_exit_hook(enum pm_state state)
{
    resume_count++;
}

/* Define the notifier structure */
static struct pm_notifier my_pm_notifier = {
    .state_entry = my_pm_state_enter_hook,
    .state_exit = my_pm_state_exit_hook,
};

static void pm_init(void) {
	pm_notifier_register(&my_pm_notifier);
}
SYS_INIT(pm_init, POST_KERNEL, 50);

static int command_mypm(int argc, const char **argv)
{
	if (argc < 1)
		return EC_ERROR_PARAM_COUNT;

	LOG_ERR("System has suspend %u times and resumed %u times.\n", suspend_count, resume_count);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(mypm, command_mypm,
			"123",
			"456.");
