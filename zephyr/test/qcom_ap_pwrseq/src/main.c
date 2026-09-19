/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_interface.h"
#include "chipset.h"
#include "hooks.h"
#include "mock_chipset.h"
#include "mock_input.h"
#include "mock_switchcap.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define AP_PWRSEQ_NODE DT_NODELABEL(qcom_ap_pwrseq)

static const struct device *const ap_pwrseq_dev = DEVICE_DT_GET(AP_PWRSEQ_NODE);
static const struct gpio_dt_spec ap_suspend_gpio =
	GPIO_DT_SPEC_GET(AP_PWRSEQ_NODE, ap_suspend_gpios);
static const struct gpio_dt_spec power_good_gpio =
	GPIO_DT_SPEC_GET(AP_PWRSEQ_NODE, power_good_gpios);
static const struct gpio_dt_spec ap_rst_gpio =
	GPIO_DT_SPEC_GET(AP_PWRSEQ_NODE, ap_rst_gpios);
static const struct gpio_dt_spec pmic_kpd_pwr_gpio =
	GPIO_DT_SPEC_GET(AP_PWRSEQ_NODE, pmic_kpd_pwr_odl_gpios);
static const struct gpio_dt_spec pmic_resin_gpio =
	GPIO_DT_SPEC_GET(AP_PWRSEQ_NODE, pmic_resin_gpios);

static void check_gpio_flags(const struct gpio_dt_spec *gpio,
			     gpio_flags_t expected_flags)
{
	gpio_flags_t flags;

	zassert_ok(gpio_emul_flags_get(gpio->port, gpio->pin, &flags));
	zassert_true((flags & expected_flags) == expected_flags);
}

static void test_before(void *f)
{
	ARG_UNUSED(f);

	/* Ensure the power sequence task is started without auto power-on. */
	system_set_reset_flags(EC_RESET_FLAG_AP_OFF);
	ap_pwrseq_task_start();

	/*
	 * If the AP is not in G3, clear power-good and switchcap-enabled
	 * states so transition_to_g3() completes immediately, then force
	 * shutdown back to G3.
	 */
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
		gpio_emul_input_set(power_good_gpio.port, power_good_gpio.pin,
				    0);
		cros_switchcap_is_enabled_fake.return_val = false;
		cros_switchcap_is_power_good_fake.return_val = false;
		chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);
		k_msleep(50);
		zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	}

	/* Restore default GPIO configurations and emulated input levels. */
	zassert_ok(gpio_pin_configure_dt(&pmic_resin_gpio, GPIO_OUTPUT_HIGH));
	zassert_ok(gpio_emul_input_set(ap_rst_gpio.port, ap_rst_gpio.pin, 1));
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 1));
	zassert_ok(gpio_emul_input_set(ap_suspend_gpio.port,
				       ap_suspend_gpio.pin, 0));

	/* Reset all mock states and hook counters. */
	mock_switchcap_reset_state();
	mock_input_reset_state();
	mock_chipset_reset_state();
}

static void power_on_to_s0(void)
{
	chipset_power_on();
	k_msleep(400);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	mock_clear_hook_notify_counts();
}

static void suspend_to_s3(void)
{
	power_on_to_s0();
	zassert_ok(gpio_emul_input_set(ap_suspend_gpio.port,
				       ap_suspend_gpio.pin, 1));
	k_msleep(50);
	zassert_true(chipset_in_state(CHIPSET_STATE_SUSPEND));
	mock_clear_hook_notify_counts();
}

static void pulse_ap_rst(void)
{
	zassert_ok(gpio_emul_input_set(ap_rst_gpio.port, ap_rst_gpio.pin, 0));
	zassert_ok(gpio_emul_input_set(ap_rst_gpio.port, ap_rst_gpio.pin, 1));
}

/*
 * Verify driver initialization and task startup without auto power-on:
 * 1. Check that the ap_pwrseq device is ready.
 * 2. Verify GPIO directions and initial output levels.
 * 3. Verify initial G3 state, switchcap disabled, and AP sleep mask cleared.
 * 4. Start the power sequence task with EC_RESET_FLAG_AP_OFF set and verify
 *    the chipset remains in G3.
 */
ZTEST(qcom_ap_pwrseq, test_init)
{
	system_set_reset_flags(EC_RESET_FLAG_AP_OFF);

	zassert_true(device_is_ready(ap_pwrseq_dev));

	check_gpio_flags(&ap_rst_gpio, GPIO_INPUT);
	check_gpio_flags(&ap_suspend_gpio, GPIO_INPUT);
	check_gpio_flags(&power_good_gpio, GPIO_INPUT);

	check_gpio_flags(&pmic_kpd_pwr_gpio, GPIO_OUTPUT);
	zassert_equal(gpio_emul_output_get(pmic_kpd_pwr_gpio.port,
					   pmic_kpd_pwr_gpio.pin),
		      1);

	check_gpio_flags(&pmic_resin_gpio, GPIO_OUTPUT);
	zassert_equal(gpio_emul_output_get(pmic_resin_gpio.port,
					   pmic_resin_gpio.pin),
		      1);

	/* Initial state after qcom_ap_pwrseq_init() is G3 */
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_true(
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON));
	zassert_false(cros_switchcap_is_enabled_fake.return_val);
	zassert_false(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);

	ap_pwrseq_task_start();
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
}

/*
 * Verify AP startup sequence:
 * 1. Start the power sequence task and wait for the G3 -> S0 transition delay.
 * 2. Verify S0 state, switchcap enabled, AP sleep mask set, and startup hooks
 *    notified.
 */
ZTEST(qcom_ap_pwrseq, test_startup)
{
	system_common_reset_state();
	cros_switchcap_is_power_good_fake.return_val = true;
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 1));

	ap_pwrseq_task_start();
	k_msleep(400);

	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON));
	zassert_false(
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_HARD_OFF));
	zassert_true(cros_switchcap_is_enabled_fake.return_val);
	zassert_true(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 1);
}

/*
 * Verify explicit AP power-on and PMIC power key handshake:
 * 1. Request power-on while PMIC power-good is deasserted and PMIC reset-in is
 *    released high.
 * 2. Verify the driver asserts the PMIC power key line low while waiting for
 *    the PMIC to power up.
 * 3. Signal PMIC power-good and verify the PMIC power key line is released,
 *    the chipset reaches S0, and startup hooks are notified.
 */
ZTEST(qcom_ap_pwrseq, test_explicit_power_on)
{
	/* Start with power_good low and pmic_resin readable high */
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 0));
	zassert_ok(gpio_pin_configure_dt(&pmic_resin_gpio,
					 GPIO_OUTPUT_HIGH | GPIO_INPUT));
	zassert_ok(gpio_emul_input_set(pmic_resin_gpio.port,
				       pmic_resin_gpio.pin, 1));

	/* Request power-on and wait until set_pmic_pwron drives kpd_pwr */
	chipset_power_on();
	k_msleep(350);

	/* Verify pmic_kpd_pwr_gpio is held low and state is G3S0 */
	zassert_equal(gpio_emul_output_get(pmic_kpd_pwr_gpio.port,
					   pmic_kpd_pwr_gpio.pin),
		      0);
	zassert_false(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON));
	zassert_false(
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_HARD_OFF));

	/* Assert power_good_gpio and verify transition to S0 completes */
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 1));
	k_msleep(100);

	zassert_equal(gpio_emul_output_get(pmic_kpd_pwr_gpio.port,
					   pmic_kpd_pwr_gpio.pin),
		      1);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(cros_switchcap_is_enabled_fake.return_val);
	zassert_true(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 1);
}

/*
 * Verify waking the AP from G3 via a power button press and release:
 * 1. Press the power button while in G3 and verify the chipset remains off
 *    until the button is released.
 * 2. Release the power button and verify it triggers the G3 -> S0 power-on
 *    sequence.
 */
ZTEST(qcom_ap_pwrseq, test_power_button_wake_g3)
{
	/* Press power button */
	mock_power_button_set(true);
	k_msleep(50);

	/* Verify press alone does not wake AP from G3 */
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 0);

	/* Release power button and verify G3 -> S0 transition */
	mock_power_button_set(false);
	k_msleep(400);

	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(cros_switchcap_is_enabled_fake.return_val);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 1);
}

/*
 * Verify waking the AP from G3 when the lid is opened:
 * 1. Close the lid while in G3 and verify the chipset remains off.
 * 2. Open the lid and verify it triggers the G3 -> S0 power-on sequence.
 */
ZTEST(qcom_ap_pwrseq, test_lid_open_wake_g3)
{
	/* Close lid in G3 */
	mock_lid_set(false);
	k_msleep(50);

	/* Verify lid close does not wake AP */
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 0);

	/* Open lid and verify G3 -> S0 transition */
	mock_lid_set(true);
	k_msleep(400);

	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(cros_switchcap_is_enabled_fake.return_val);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 1);
}

/*
 * Verify AP power-on is aborted when system power is insufficient to boot:
 * 1. Disallow AP boot and request power-on from G3.
 * 2. Wait for the boot power check to time out and verify the chipset remains
 *    in G3 without enabling switchcap or notifying chipset hooks.
 */
ZTEST(qcom_ap_pwrseq, test_startup_fail_insufficient_power)
{
	/* Simulate insufficient system power to boot AP */
	system_can_boot_ap_fake.return_val = 0;

	/* Request power-on and wait for power_is_enough() timeout */
	chipset_power_on();
	k_msleep(1800);

	/* Verify startup aborted in G3 before PRE_INIT */
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(cros_switchcap_is_enabled_fake.return_val);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 0);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 0);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 0);
}

/*
 * Verify AP power-on rolls back to G3 when switchcap power-good times out:
 * 1. Request power-on while switchcap fails to report power-good.
 * 2. Wait for the switchcap power-good check to time out and verify the chipset
 *    returns to G3 after pre-init without completing startup.
 */
ZTEST(qcom_ap_pwrseq, test_startup_fail_switchcap)
{
	/* Simulate switchcap power-good failure */
	cros_switchcap_is_power_good_fake.return_val = false;

	/* Request power-on and wait for switchcap PG timeout (100ms) */
	chipset_power_on();
	k_msleep(250);

	/* Verify rollback to G3 */
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 0);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 0);
}

/*
 * Verify AP power-on rolls back to G3 when PMIC reset-in is held low:
 * 1. Request power-on with PMIC power-good deasserted and PMIC reset-in low,
 *    preventing PMIC power-on.
 * 2. Verify the driver disables switchcap and returns the chipset to G3 without
 *    completing startup.
 */
ZTEST(qcom_ap_pwrseq, test_startup_fail_pmic_resin_low)
{
	/* Set power_good low with pmic_resin reading low (0) */
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 0));

	/* Request power-on and wait for PMIC fail and rollback to G3 */
	chipset_power_on();
	k_msleep(900);

	/* Verify switchcap is turned back off and AP is in G3 */
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(cros_switchcap_is_enabled_fake.return_val);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 0);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 0);
}

/*
 * Verify AP power-on rolls back to G3 when PMIC power-good times out:
 * 1. Request power-on while PMIC power-good remains deasserted past the
 *    response timeout.
 * 2. Verify the driver releases the PMIC power key line, disables switchcap,
 *    and returns the chipset to G3 without completing startup.
 */
ZTEST(qcom_ap_pwrseq, test_startup_fail_pmic_timeout)
{
	/* Set power_good low (and keep it low) with pmic_resin high */
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 0));
	zassert_ok(gpio_pin_configure_dt(&pmic_resin_gpio,
					 GPIO_OUTPUT_HIGH | GPIO_INPUT));
	zassert_ok(gpio_emul_input_set(pmic_resin_gpio.port,
				       pmic_resin_gpio.pin, 1));

	/* Request power-on and wait for PMIC power-on timeout & rollback */
	chipset_power_on();
	k_msleep(1250);

	/* Verify kpd_pwr is released, switchcap is off, and AP is in G3 */
	zassert_equal(gpio_emul_output_get(pmic_kpd_pwr_gpio.port,
					   pmic_kpd_pwr_gpio.pin),
		      1);
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(cros_switchcap_is_enabled_fake.return_val);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 0);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 0);
}

/*
 * Verify AP suspend transition (S0 -> S3) when AP_SUSPEND is asserted:
 * 1. Power on the chipset to S0 and assert the AP suspend signal high.
 * 2. Verify the chipset transitions to S3, enables EC deep sleep, and notifies
 *    the suspend hook.
 */
ZTEST(qcom_ap_pwrseq, test_suspend_s0_to_s3)
{
	/* Enter S0 state */
	power_on_to_s0();

	/* Assert AP_SUSPEND high to trigger S0 -> S3 */
	zassert_ok(gpio_emul_input_set(ap_suspend_gpio.port,
				       ap_suspend_gpio.pin, 1));
	k_msleep(50);

	/* Verify S3 state, sleep mask cleared, and suspend hook notified */
	zassert_true(chipset_in_state(CHIPSET_STATE_SUSPEND));
	zassert_true(
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_SUSPEND));
	zassert_false(chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON));
	zassert_false(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SUSPEND), 1);
}

/*
 * Verify AP resume transition (S3 -> S0) when AP_SUSPEND is deasserted:
 * 1. Place the chipset in S3 suspend and deassert the AP suspend signal low.
 * 2. Verify the chipset resumes to S0, disables EC deep sleep, and notifies the
 *    resume hook without re-running startup.
 */
ZTEST(qcom_ap_pwrseq, test_resume_ap_suspend_deassert)
{
	/* Enter S3 state */
	suspend_to_s3();

	/* Deassert AP_SUSPEND low to trigger S3 -> S0 */
	zassert_ok(gpio_emul_input_set(ap_suspend_gpio.port,
				       ap_suspend_gpio.pin, 0));
	k_msleep(50);

	/* Verify S0 state, sleep mask set, and resume hook notified */
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 0);
}

/*
 * Verify waking the AP from S3 suspend when the lid is opened:
 * 1. Place the chipset in S3 suspend, close the lid, and verify it remains
 *    suspended.
 * 2. Open the lid and verify the chipset resumes to S0, disables EC deep sleep,
 *    and notifies the resume hook.
 */
ZTEST(qcom_ap_pwrseq, test_resume_lid_open)
{
	/* Enter S3 state */
	suspend_to_s3();

	/* Close lid in S3 and verify chipset stays suspended */
	mock_lid_set(false);
	k_msleep(50);
	zassert_true(chipset_in_state(CHIPSET_STATE_SUSPEND));
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 0);

	/* Open lid and verify S3 -> S0 resume */
	mock_lid_set(true);
	k_msleep(50);

	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 0);
}

/*
 * Verify waking the AP from S3 suspend when the power button is pressed:
 * 1. Place the chipset in S3 suspend and press the power button.
 * 2. Verify the chipset immediately resumes to S0 on the button press, disables
 *    EC deep sleep, and notifies the resume hook.
 * 3. Release the power button to clear the forced-shutdown timer.
 */
ZTEST(qcom_ap_pwrseq, test_resume_power_button_press)
{
	/* Enter S3 state */
	suspend_to_s3();

	/* Press power button to trigger immediate S3 -> S0 resume */
	mock_power_button_set(true);
	k_msleep(50);

	/* Verify S0 state, sleep mask set, and resume hook notified */
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 0);

	/* Release power button to cancel the 8s hold timer */
	mock_power_button_set(false);
	k_msleep(50);
}

/*
 * Verify forced AP shutdown from S0:
 * 1. Power on the chipset to S0 with PMIC reset-in readable high, then request
 *    a forced shutdown.
 * 2. Verify the driver notifies suspend and shutdown hooks and asserts both
 *    PMIC power key and PMIC reset-in lines low while waiting for PMIC
 *    power-good to drop.
 * 3. Drop PMIC and switchcap power-good signals and verify the PMIC lines are
 *    released high, switchcap is disabled, EC deep sleep is enabled, the
 *    shutdown-complete hook is notified, and the chipset reaches G3.
 */
ZTEST(qcom_ap_pwrseq, test_force_shutdown_from_s0)
{
	/* Enter S0 with pmic_resin readable high for set_pmic_pwron(false) */
	power_on_to_s0();
	zassert_ok(gpio_pin_configure_dt(&pmic_resin_gpio,
					 GPIO_OUTPUT_HIGH | GPIO_INPUT));
	zassert_ok(gpio_emul_input_set(pmic_resin_gpio.port,
				       pmic_resin_gpio.pin, 1));

	/* Request forced shutdown and check active PMIC power-off assertion */
	chipset_force_shutdown(CHIPSET_SHUTDOWN_CONSOLE_CMD);
	k_msleep(50);

	zassert_equal(gpio_emul_output_get(pmic_kpd_pwr_gpio.port,
					   pmic_kpd_pwr_gpio.pin),
		      0);
	zassert_equal(gpio_emul_output_get(pmic_resin_gpio.port,
					   pmic_resin_gpio.pin),
		      0);
	zassert_false(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_true(
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON));
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SUSPEND), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN), 1);
	zassert_equal(
		mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN_COMPLETE), 0);

	/* Drop power_good and switchcap PG to complete shutdown to G3 */
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 0));
	cros_switchcap_is_power_good_fake.return_val = false;
	k_msleep(650);

	/* Verify PMIC lines released, switchcap disabled, and G3 reached */
	zassert_equal(gpio_emul_output_get(pmic_kpd_pwr_gpio.port,
					   pmic_kpd_pwr_gpio.pin),
		      1);
	zassert_equal(gpio_emul_output_get(pmic_resin_gpio.port,
					   pmic_resin_gpio.pin),
		      1);
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(cros_switchcap_is_enabled_fake.return_val);
	zassert_false(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(
		mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN_COMPLETE), 1);
}

/*
 * Verify forced AP shutdown from S3 suspend:
 * 1. Place the chipset in S3 suspend and request a forced shutdown.
 * 2. Drop power-good signals and verify the chipset transitions to G3,
 *    notifying shutdown and shutdown-complete hooks without re-notifying the
 *    suspend hook.
 */
ZTEST(qcom_ap_pwrseq, test_force_shutdown_from_s3)
{
	/* Enter S3 state */
	suspend_to_s3();

	/* Request forced shutdown from S3 and drop power-good signals */
	chipset_force_shutdown(CHIPSET_SHUTDOWN_CONSOLE_CMD);
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 0));
	cros_switchcap_is_power_good_fake.return_val = false;
	k_msleep(50);

	/* Verify intermediate S3G3 transition state */
	zassert_false(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_true(
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_SUSPEND));

	k_msleep(450);

	/* Verify G3 reached without firing a duplicate suspend hook */
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(cros_switchcap_is_enabled_fake.return_val);
	zassert_false(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SUSPEND), 0);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN), 1);
	zassert_equal(
		mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN_COMPLETE), 1);
}

/*
 * Verify releasing the power button before 8 seconds in S0 cancels forced
 * shutdown:
 * 1. Power on the chipset to S0, press the power button briefly, and release it
 *    before the 8-second hold timeout.
 * 2. Wait past the 8-second window and verify the chipset remains in S0 without
 *    shutting down.
 */
ZTEST(qcom_ap_pwrseq, test_power_button_hold_shutdown_cancel)
{
	/* Enter S0 state */
	power_on_to_s0();

	/* Press and release power button before 8s timeout */
	mock_power_button_set(true);
	k_msleep(100);
	mock_power_button_set(false);

	/* Wait past 8s deadline and verify AP stays in S0 */
	k_msleep(8000);

	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN), 0);
}

/*
 * Verify holding the power button for 8 seconds in S0 forces shutdown to G3:
 * 1. Power on the chipset to S0 and hold the power button pressed.
 * 2. Verify the chipset remains in S0 prior to the 8-second hold timeout.
 * 3. Wait for the 8-second timeout to expire and verify the driver forces
 *    shutdown to G3, disables switchcap, and notifies shutdown hooks.
 */
ZTEST(qcom_ap_pwrseq, test_power_button_hold_shutdown_s0)
{
	/* Enter S0 state */
	power_on_to_s0();

	/* Hold power button and verify AP is still in S0 just before 8s */
	mock_power_button_set(true);
	k_msleep(7900);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));

	/* Allow 8s hold timeout to trigger shutdown, then clear PG signals */
	k_msleep(200);
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 0));
	cros_switchcap_is_power_good_fake.return_val = false;
	k_msleep(600);
	mock_power_button_set(false);

	/* Verify forced shutdown to G3 completed */
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(cros_switchcap_is_enabled_fake.return_val);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SUSPEND), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN), 1);
	zassert_equal(
		mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN_COMPLETE), 1);
}

/*
 * Verify a transient POWER_GOOD drop (< 200ms) in S0 reflects a hard reset
 * rather than a shutdown and does not power off the AP:
 * 1. Power on the chipset to S0 and drop PMIC power-good low to simulate a hard
 *    reset.
 * 2. Restore PMIC power-good high before the 200ms debounce window expires.
 * 3. Wait past the 200ms debounce window and verify the chipset remains in S0
 *    with switchcap enabled without initiating shutdown.
 */
ZTEST(qcom_ap_pwrseq, test_power_good_transient_drop_s0)
{
	/* Enter S0 state */
	power_on_to_s0();

	/* Drop power_good briefly (< 200ms) to simulate PSCI hard reset dip */
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 0));
	k_msleep(100);
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 1));

	/* Wait past 200ms debounce timeout and verify AP remains in S0 */
	k_msleep(250);

	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(cros_switchcap_is_enabled_fake.return_val);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN), 0);
}

/*
 * Verify sustained POWER_GOOD loss (> 200ms) in S0 shuts down the AP to G3:
 * 1. Power on the chipset to S0 and drop PMIC power-good low.
 * 2. Verify the chipset remains in S0 during the 200ms debounce window.
 * 3. Wait for the debounce timeout and power-off sequence to complete, and
 *    verify the chipset transitions to G3 with switchcap disabled and shutdown
 *    hooks notified.
 */
ZTEST(qcom_ap_pwrseq, test_power_good_lost_s0)
{
	/* Enter S0 state */
	power_on_to_s0();

	/* Drop power_good and verify AP stays in S0 during 200ms debounce */
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 0));
	cros_switchcap_is_power_good_fake.return_val = false;
	k_msleep(100);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));

	/* Wait for debounce expiry and transition to G3 */
	k_msleep(600);

	/* Verify G3 state, switchcap disabled, and shutdown hooks notified */
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(cros_switchcap_is_enabled_fake.return_val);
	zassert_false(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SUSPEND), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN), 1);
	zassert_equal(
		mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN_COMPLETE), 1);
}

/*
 * Verify sustained POWER_GOOD loss (> 200ms) in S3 shuts down the AP to G3:
 * 1. Place the chipset in S3 suspend and drop PMIC power-good low.
 * 2. Wait for the 200ms debounce timeout and power-off sequence to complete.
 * 3. Verify the chipset transitions to G3 and notifies shutdown hooks without
 *    re-notifying the suspend hook.
 */
ZTEST(qcom_ap_pwrseq, test_power_good_lost_s3)
{
	/* Enter S3 state */
	suspend_to_s3();

	/* Drop power_good and wait for debounce and shutdown to G3 */
	zassert_ok(gpio_emul_input_set(power_good_gpio.port,
				       power_good_gpio.pin, 0));
	cros_switchcap_is_power_good_fake.return_val = false;
	k_msleep(700);

	/* Verify G3 state, switchcap disabled, and hooks notified */
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF));
	zassert_false(cros_switchcap_is_enabled_fake.return_val);
	zassert_false(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SUSPEND), 0);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN), 1);
	zassert_equal(
		mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN_COMPLETE), 1);
}

/*
 * Verify warm AP reset via chipset_reset():
 * 1. Power on the chipset to S0 and request a chipset reset.
 * 2. Verify the driver pulses the PMIC reset-in line low for 20ms and then
 *    releases it high while waiting for AP_RST_L to assert low.
 * 3. Assert AP_RST_L low and verify the warm reset succeeds with the chipset
 *    remaining in S0 without cycling power rails.
 */
ZTEST(qcom_ap_pwrseq, test_warm_reset)
{
	/* Enter S0 state */
	power_on_to_s0();

	/* Request reset and verify pmic_resin is pulsed low during 20ms window
	 */
	chipset_reset(CHIPSET_RESET_CONSOLE_CMD);
	k_msleep(10);
	zassert_equal(gpio_emul_output_get(pmic_resin_gpio.port,
					   pmic_resin_gpio.pin),
		      0);

	/* Wait for 20ms pulse to end, verify release, and assert ap_rst low */
	k_msleep(20);
	zassert_equal(gpio_emul_output_get(pmic_resin_gpio.port,
					   pmic_resin_gpio.pin),
		      1);
	zassert_ok(gpio_emul_input_set(ap_rst_gpio.port, ap_rst_gpio.pin, 0));
	k_msleep(50);
	zassert_ok(gpio_emul_input_set(ap_rst_gpio.port, ap_rst_gpio.pin, 1));
	k_msleep(50);

	/* Verify AP stays in S0 without cold-reset shutdown */
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(cros_switchcap_is_enabled_fake.return_val);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN), 0);
}

/*
 * Verify cold AP reset fallback when warm reset times out:
 * 1. Power on the chipset to S0 and request a chipset reset while keeping
 *    AP_RST_L high so warm reset detection times out.
 * 2. Wait for the warm reset timeout and subsequent S0 -> G3 -> S0 cold reboot
 *    sequence to complete.
 * 3. Verify the driver cycles through G3 and returns the chipset to S0,
 *    notifying both shutdown and startup hooks.
 */
ZTEST(qcom_ap_pwrseq, test_cold_reset_fallback)
{
	/* Enter S0 state with ap_rst kept high so warm_reset_seq times out */
	power_on_to_s0();

	/* Request reset and wait for warm timeout + S0 -> G3 -> S0 cycle */
	chipset_reset(CHIPSET_RESET_CONSOLE_CMD);
	k_msleep(1500);

	/* Verify full cold reboot cycle completed back to S0 */
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(cros_switchcap_is_enabled_fake.return_val);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SUSPEND), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN), 1);
	zassert_equal(
		mock_get_hook_notify_count(HOOK_CHIPSET_SHUTDOWN_COMPLETE), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 1);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 1);
}

/*
 * Verify autonomous AP reset detection on 3 AP_RST_L falling edges in S0:
 * 1. Power on the chipset to S0 and generate 2 falling edges on AP_RST_L,
 *    verifying the reset hook does not fire immediately.
 * 2. Generate the 3rd falling edge and verify the reset hook fires immediately
 *    without waiting for the 450ms debounce timeout.
 */
ZTEST(qcom_ap_pwrseq, test_ap_rst_three_pulses_s0)
{
	/* Enter S0 state */
	power_on_to_s0();

	/* Two falling edges should still wait on debounce timer */
	pulse_ap_rst();
	pulse_ap_rst();
	k_msleep(20);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESET), 0);

	/* Third falling edge triggers immediate HOOK_CHIPSET_RESET */
	pulse_ap_rst();
	k_msleep(50);

	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESET), 1);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
}

/*
 * Verify autonomous AP reset detection after the 450ms debounce timeout when
 * fewer than 3 falling edges occur on AP_RST_L:
 * 1. Power on the chipset to S0 and generate a single falling edge on AP_RST_L.
 * 2. Verify the reset hook has not fired before the 450ms debounce window.
 * 3. Wait past the 450ms debounce timeout and verify the reset hook is notified
 *    while the chipset remains in S0.
 */
ZTEST(qcom_ap_pwrseq, test_ap_rst_debounce_timeout_s0)
{
	/* Enter S0 state */
	power_on_to_s0();

	/* Single falling edge on ap_rst */
	pulse_ap_rst();
	k_msleep(200);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESET), 0);

	/* Wait for 450ms debounce timeout to expire */
	k_msleep(350);

	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESET), 1);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
}

/*
 * Verify an autonomous AP reset during S3 suspend wakes the AP to S0:
 * 1. Place the chipset in S3 suspend and generate 3 falling edges on AP_RST_L.
 * 2. Verify the driver notifies the reset hook and resumes the chipset from S3
 *    to S0.
 */
ZTEST(qcom_ap_pwrseq, test_ap_rst_in_s3_wakes_to_s0)
{
	/* Enter S3 state */
	suspend_to_s3();

	/* Three falling edges on ap_rst while in S3 */
	pulse_ap_rst();
	pulse_ap_rst();
	pulse_ap_rst();
	k_msleep(50);

	/* Verify HOOK_CHIPSET_RESET fired and AP resumed to S0 */
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESET), 1);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
	zassert_true(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_RESUME), 1);
}

ZTEST_SUITE(qcom_ap_pwrseq, NULL, NULL, test_before, NULL, NULL);
