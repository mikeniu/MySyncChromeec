/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_events.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "tablet_mode.h"
#include "test_state.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VALUE_FUNC(bool, cros_cbi_ssfc_check_match, enum cbi_ssfc_value_id);
FAKE_VOID_FUNC(gmr_tablet_switch_disable);
FAKE_VOID_FUNC(bma4xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lis2dw12_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lsm6dsm_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(xhci_interrupt, enum gpio_signal);

static uint32_t mock_form_factor = CONVERTIBLE;
static int mock_cbi_ret;

static int mock_cros_cbi_get_fw_config(enum cbi_fw_config_field_id field_id,
				       uint32_t *value)
{
	if (field_id != FORM_FACTOR) {
		return -EINVAL;
	}
	if (mock_cbi_ret != 0) {
		return mock_cbi_ret;
	}
	*value = mock_form_factor;
	return 0;
}

static void pulse_sensor_irqs(void)
{
	const struct gpio_dt_spec *lid_irq =
		GPIO_DT_FROM_NODELABEL(gpio_lid_accel_int_ec_l);
	const struct gpio_dt_spec *imu_irq =
		GPIO_DT_FROM_NODELABEL(gpio_imu_int_ec_l);

	zassert_ok(gpio_emul_input_set_dt(lid_irq, 1));
	zassert_ok(gpio_emul_input_set_dt(imu_irq, 1));
	k_sleep(K_MSEC(1));
	zassert_ok(gpio_emul_input_set_dt(lid_irq, 0));
	zassert_ok(gpio_emul_input_set_dt(imu_irq, 0));
	k_sleep(K_MSEC(1));
	zassert_ok(gpio_emul_input_set_dt(lid_irq, 1));
	zassert_ok(gpio_emul_input_set_dt(imu_irq, 1));
}

static void anakin_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(cros_cbi_ssfc_check_match);
	RESET_FAKE(gmr_tablet_switch_disable);
	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dsm_interrupt);

	mock_form_factor = CONVERTIBLE;
	mock_cbi_ret = 0;
	cros_cbi_get_fw_config_fake.custom_fake = mock_cros_cbi_get_fw_config;
}

ZTEST(anakin, test_clamshell_sensor_irqs_stay_disabled_on_ap_startup)
{
	mock_form_factor = CLAMSHELL;
	hook_notify(HOOK_INIT);

	zassert_equal(gmr_tablet_switch_disable_fake.call_count, 1);
	zassert_equal(motion_sensor_count, 0);

	/*
	 * Transition AP to startup and shutdown; on a clamshell SKU without
	 * motion sensors populated, int_lid_accel and int_imu must remain
	 * disabled.
	 */
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);

	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dsm_interrupt);

	pulse_sensor_irqs();

	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 0);
	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dsm_interrupt_fake.call_count, 0);

	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
}

ZTEST(anakin, test_convertible_sensor_irqs_and_ssfc)
{
	mock_form_factor = CONVERTIBLE;
	cros_cbi_ssfc_check_match_fake.return_val = true;
	hook_notify(HOOK_INIT);

	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	pulse_sensor_irqs();

	zassert_equal(bma4xx_interrupt_fake.call_count, 1);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 0);
	zassert_equal(bmi3xx_interrupt_fake.call_count, 1);
	zassert_equal(lsm6dsm_interrupt_fake.call_count, 0);

	/* Switch to alternate sensors (lis2dw12 + lsm6dsm) */
	cros_cbi_ssfc_check_match_fake.return_val = false;
	hook_notify(HOOK_INIT);

	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dsm_interrupt);

	pulse_sensor_irqs();

	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 1);
	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dsm_interrupt_fake.call_count, 1);

	/* Verify AP_POWER_SHUTDOWN disables both interrupts */
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);

	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dsm_interrupt);

	pulse_sensor_irqs();

	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 0);
	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dsm_interrupt_fake.call_count, 0);
}

ZTEST(anakin, test_cbi_read_error)
{
	mock_cbi_ret = -EINVAL;
	hook_notify(HOOK_INIT);
	zassert_equal(gmr_tablet_switch_disable_fake.call_count, 0);
}

ZTEST_SUITE(anakin, skywalker_predicate_post_main, NULL, anakin_before, NULL,
	    NULL);
