/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "driver/charger/rt9490.h"
#include "hooks.h"
#include "ite_hooks.h"
#include "system.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>

DECLARE_FAKE_VALUE_FUNC(int, board_get_version);

FAKE_VALUE_FUNC(int, pinctrl_configure_pins, const pinctrl_soc_pin_t *, uint8_t,
		uintptr_t);

ZTEST(krabby_hooks, test_board_backlight_handler)
{
	const struct gpio_dt_spec *bl_en =
		GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od);

	ap_power_ev_send_callbacks(AP_POWER_RESUME);
	zassert_equal(gpio_emul_output_get(bl_en->port, bl_en->pin), 1);

	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(gpio_emul_output_get(bl_en->port, bl_en->pin), 0);

	/* Trigger switch default branch */
	STRUCT_SECTION_FOREACH(ap_power_ev_callback, cb)
	{
		struct ap_power_ev_data data = { .event = AP_POWER_PRE_INIT };
		cb->handler((struct ap_power_ev_callback *)cb, data);
	}
}

ZTEST(krabby_hooks, test_board_i2c3_ctrl)
{
	pinctrl_configure_pins_fake.call_count = 0;

	hook_notify(HOOK_CHIPSET_PRE_INIT);
	zassert_equal(pinctrl_configure_pins_fake.call_count, 1);

	hook_notify(HOOK_CHIPSET_HARD_OFF);
	zassert_equal(pinctrl_configure_pins_fake.call_count, 2);
}

ZTEST(krabby_hooks, test_board_suspend_handler)
{
	const struct gpio_dt_spec *en_5v =
		GPIO_DT_FROM_NODELABEL(gpio_en_5v_usm);

	ap_power_ev_send_callbacks(AP_POWER_RESUME);
	zassert_equal(gpio_emul_output_get(en_5v->port, en_5v->pin), 1);

	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(gpio_emul_output_get(en_5v->port, en_5v->pin), 0);

	/* Trigger switch default branch */
	STRUCT_SECTION_FOREACH(ap_power_ev_callback, cb)
	{
		struct ap_power_ev_data data = { .event = AP_POWER_PRE_INIT };
		cb->handler((struct ap_power_ev_callback *)cb, data);
	}
}

ZTEST(krabby_hooks, test_board_rt9490_adc_control)
{
	board_get_version_fake.return_val = 0;
	board_rt9490_adc_control();

	board_get_version_fake.return_val = 1;
	board_rt9490_adc_control();

	hook_notify(HOOK_AC_CHANGE);
	hook_notify(HOOK_INIT);
}

static void krabby_hooks_before(void *fixture)
{
	RESET_FAKE(board_get_version);
	RESET_FAKE(pinctrl_configure_pins);
}

ZTEST_SUITE(krabby_hooks, NULL, NULL, krabby_hooks_before, NULL, NULL);
