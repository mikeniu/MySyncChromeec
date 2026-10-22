/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "battery.h"
#include "charge_manager.h"
#include "driver/ppc/syv682x.h"
#include "driver/ppc/syv682x_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_syv682x.h"
#include "fakes.h"
#include "hooks.h"
#include "i2c/i2c.h"
#include "system.h"
#include "test_state.h"
#include "usb_pd.h"
#include "usb_pd_policy.h"
#include "usbc_ppc.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>

ZTEST(geralt_common, test_none)
{
	/* placeholder test */
}

ZTEST(geralt_common, test_port_frs_disable_until_source_on)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/* all ports delay the FRS disabled pins */
		zassert_true(port_frs_disable_until_source_on(i));
	}
}

ZTEST(geralt_common, test_hooks)
{
	const struct gpio_dt_spec *en_5v =
		GPIO_DT_FROM_NODELABEL(gpio_en_5v_usm);

	hook_notify(HOOK_INIT);
	hook_notify(HOOK_CHIPSET_PRE_INIT);
	zassert_equal(pinctrl_configure_pins_fake.call_count, 1);
	hook_notify(HOOK_CHIPSET_HARD_OFF);
	zassert_equal(pinctrl_configure_pins_fake.call_count, 2);
	hook_notify(HOOK_AC_CHANGE);

	ap_power_ev_send_callbacks(AP_POWER_RESUME);
	zassert_equal(gpio_emul_output_get(en_5v->port, en_5v->pin), 1);

	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(gpio_emul_output_get(en_5v->port, en_5v->pin), 0);
}

ZTEST(geralt_common, test_hibernate)
{
	const struct gpio_dt_spec *en_pp5000 =
		GPIO_DT_FROM_NODELABEL(en_pp5000_z1_l);
	const struct gpio_dt_spec *en_ulp = GPIO_DT_FROM_NODELABEL(en_ulp);

	gpio_pin_set_dt(en_pp5000, 0);
	gpio_pin_set_dt(en_ulp, 0);

	board_hibernate_late();

	zassert_equal(gpio_emul_output_get(en_pp5000->port, en_pp5000->pin), 1);
	zassert_equal(gpio_emul_output_get(en_ulp->port, en_ulp->pin), 1);
}

static void geralt_common_before(void *fixture)
{
	RESET_FAKE(pinctrl_configure_pins);
}

ZTEST_SUITE(geralt_common, geralt_predicate_post_main, NULL,
	    geralt_common_before, NULL, NULL);
