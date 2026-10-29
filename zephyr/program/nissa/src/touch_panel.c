/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "cros_cbi.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/* touch panel power sequence control */

#if DT_NODE_EXISTS(DT_NODELABEL(gpio_ec_touch_en))

#define TOUCH_ENABLE_DELAY_MS (500 * USEC_PER_MSEC)

void touch_enable(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en), 1);
}
DECLARE_DEFERRED(touch_enable);

void soc_edp_bl_interrupt(enum gpio_signal signal)
{
	int state;

	if (signal != GPIO_SIGNAL(DT_NODELABEL(gpio_soc_edp_bl_en)))
		return;

	state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_soc_edp_bl_en));

	if (state) {
		hook_call_deferred(&touch_enable_data, TOUCH_ENABLE_DELAY_MS);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en), 0);
	}

	LOG_INF("%s: %d", __func__, state);
}

static void touch_enable_init(void)
{
#if DT_NODE_EXISTS(DT_PATH(nissa_fw_config, touch_en))
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_TOUCH_EN, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_TOUCH_EN);
		return;
	}

	LOG_INF("%s: %sable", __func__,
		(val == FW_TOUCH_EN_ENABLE) ? "en" : "dis");

	if (val != FW_TOUCH_EN_ENABLE)
		return;
#endif

	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_soc_edp_bl_en));
}
DECLARE_HOOK(HOOK_INIT, touch_enable_init, HOOK_PRIO_DEFAULT);

#elif DT_NODE_EXISTS(DT_NODELABEL(gpio_tp_vcc_od))

static bool value_en;

static void set_tp_en_pin(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(gpio_tchscr_report_en))
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en),
			value_en);
#else
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_en_od), value_en);
#endif
}
DECLARE_DEFERRED(set_tp_en_pin);

static void set_bl_en_pin(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bl_en_od), value_en);
}
DECLARE_DEFERRED(set_bl_en_pin);

void ap_bl_en_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ap_bl_en_odl))) {
		value_en = true;
		hook_call_deferred(&set_bl_en_pin_data, 0);
		hook_call_deferred(&set_tp_en_pin_data, 30 * USEC_PER_MSEC);
	} else {
		value_en = false;
		hook_call_deferred(&set_tp_en_pin_data, 0);
		hook_call_deferred(&set_bl_en_pin_data, 102 * USEC_PER_MSEC);
	}
}

static void ap_bl_en_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ap_bl_en));
}
DECLARE_HOOK(HOOK_INIT, ap_bl_en_init, HOOK_PRIO_DEFAULT);

static void board_backlight_handler(struct ap_power_ev_callback *cb,
				    struct ap_power_ev_data data)
{
	int value;

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		/* Called on AP S3 -> S0 transition */
		value = 1;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_vcc_od), value);
		break;

	case AP_POWER_SUSPEND:
		/* Called on AP S0 -> S3 transition */
		value = 0;
		break;
	case AP_POWER_SHUTDOWN:
		value = 0;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_vcc_od), value);
		break;
	}
}
AP_POWER_EVENT_CALLBACK_DEFINE(board_backlight_handler, AP_POWER_RESUME,
			       AP_POWER_SUSPEND, AP_POWER_SHUTDOWN);

#endif
