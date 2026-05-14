/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "power_button.h"
#include "tablet_mode.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, tablet_get_mode);
FAKE_VOID_FUNC(power_button_simulate_press, uint32_t);

static const struct gpio_dt_spec report_en_spec =
	GPIO_DT_SPEC_GET(DT_NODELABEL(wot), report_en_gpios);
static const struct gpio_dt_spec wot_l_spec =
	GPIO_DT_SPEC_GET(DT_NODELABEL(wot), wot_l_gpios);
static const struct gpio_dt_spec bl_detect_spec =
	GPIO_DT_SPEC_GET(DT_NODELABEL(wot), bl_detect_gpios);
static const struct gpio_dt_spec tchscr_int_spec =
	GPIO_DT_SPEC_GET(DT_NODELABEL(wot), tchscr_int_gpios);

static void wot_test_before(void *fixture)
{
	RESET_FAKE(tablet_get_mode);
	RESET_FAKE(power_button_simulate_press);
	FFF_RESET_HISTORY();

	gpio_emul_input_set(bl_detect_spec.port, bl_detect_spec.pin, 1);
	gpio_emul_input_set(tchscr_int_spec.port, tchscr_int_spec.pin, 1);

	k_msleep(150);
}

ZTEST_SUITE(wot_driver_tests, NULL, NULL, wot_test_before, NULL, NULL);

ZTEST(wot_driver_tests, test_suspend_clamshell_mode)
{
	tablet_get_mode_fake.return_val = 0;
	gpio_emul_input_set(bl_detect_spec.port, bl_detect_spec.pin, 0);
	k_msleep(150);

	zassert_equal(gpio_emul_output_get(report_en_spec.port,
					   report_en_spec.pin),
		      0, "report_en should be 0");
}

ZTEST(wot_driver_tests, test_suspend_tablet_mode)
{
	tablet_get_mode_fake.return_val = 1;
	gpio_emul_input_set(bl_detect_spec.port, bl_detect_spec.pin, 0);
	k_msleep(150);

	zassert_equal(gpio_emul_output_get(report_en_spec.port,
					   report_en_spec.pin),
		      1, "report_en should be 1");
	zassert_equal(gpio_emul_output_get(wot_l_spec.port, wot_l_spec.pin), 0,
		      "wot_l should be 0 (Active)");
}

ZTEST(wot_driver_tests, test_resume_normal_mode)
{
	tablet_get_mode_fake.return_val = 1;
	gpio_emul_input_set(bl_detect_spec.port, bl_detect_spec.pin, 0);
	k_msleep(150);

	gpio_emul_input_set(bl_detect_spec.port, bl_detect_spec.pin, 1);
	k_msleep(150);

	zassert_equal(gpio_emul_output_get(report_en_spec.port,
					   report_en_spec.pin),
		      1, "report_en should be 1 in S0");
}

ZTEST(wot_driver_tests, test_touch_interrupt_fires_once)
{
	tablet_get_mode_fake.return_val = 1;
	gpio_emul_input_set(bl_detect_spec.port, bl_detect_spec.pin, 0);
	k_msleep(150);

	gpio_emul_input_set(tchscr_int_spec.port, tchscr_int_spec.pin, 0);
	k_msleep(150);

	zassert_equal(power_button_simulate_press_fake.call_count, 1,
		      "Wake event should trigger");
}
