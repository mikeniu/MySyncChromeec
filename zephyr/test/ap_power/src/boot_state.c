/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Tests for detecting the AP power state when the EC boots.
 *
 * The EC can be reset while the AP stays powered (e.g. EC firmware update or
 * EC watchdog). On boot, chipset_pwr_seq_get_state() reads the power signals
 * to determine which state the AP is in, so the power sequence resumes from
 * that state instead of shutting the AP down.
 *
 * Each scenario presets the power signals in a pre-main suite, then checks
 * the resulting AP power state after ec_app_main() has started the power
 * sequence.
 */

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_interface.h"
#include "power_signals.h"
#include "test_state.h"

#include <zephyr/drivers/emul_stub_device.h>
#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/espi_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <ap_power_override_functions.h>

static int power_shutdown_count;
static int power_hard_off_count;
static int power_suspend_count;

static void boot_state_ev_handler(struct ap_power_ev_callback *callback,
				  struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_SHUTDOWN:
		power_shutdown_count++;
		break;
	case AP_POWER_HARD_OFF:
		power_hard_off_count++;
		break;
	case AP_POWER_SUSPEND:
		power_suspend_count++;
		break;
	default:
		break;
	}
}
AP_POWER_EVENT_CALLBACK_DEFINE(boot_state_ev_handler, AP_POWER_SHUTDOWN,
			       AP_POWER_HARD_OFF, AP_POWER_SUSPEND);

static const struct device *const espi =
	DEVICE_DT_GET_ANY(zephyr_espi_emul_controller);

static void set_input(const struct gpio_dt_spec *spec, int value)
{
	zassert_ok(gpio_emul_input_set(spec->port, spec->pin, value));
}

/*
 * Drive the SLP_Sx virtual wires. The wires are active low, so
 * asserted == level 0.
 */
static void __maybe_unused send_slp_vw(enum espi_vwire_signal vw, bool asserted)
{
	zassert_ok(emul_espi_host_send_vw(espi, vw, asserted ? 0 : 1));
}

/*
 * Present the primary rails as on and RSMRST released to the PCH, which is
 * the minimum needed for the VW signals to be readable.
 */
static void __maybe_unused preset_rails_on(void)
{
	const struct gpio_dt_spec rsmrst_pwrgd =
		GPIO_DT_SPEC_GET(DT_NODELABEL(pwr_rsmrst_pwrgd), gpios);

	set_input(&rsmrst_pwrgd, 1);
	zassert_ok(power_signal_set(PWR_EN_PP3300_A, 1));
	zassert_ok(power_signal_set(PWR_EC_PCH_RSMRST, 0));
}

static void __maybe_unused preset_all_sys_pwrgd(int value)
{
	const struct gpio_dt_spec all_sys_pwrgd =
		GPIO_DT_SPEC_GET(DT_NODELABEL(pwr_all_sys_pwrgd), gpios);

	set_input(&all_sys_pwrgd, value);
}

#if defined(CONFIG_TEST_AP_POWER_BOOT_S0)
ZTEST(boot_state_preset, test_preset_s0)
{
	preset_rails_on();
	preset_all_sys_pwrgd(1);
	zassert_ok(power_signal_set(PWR_PCH_PWROK, 1));
	send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S3, false);
	send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S4, false);
	send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S5, false);
}

ZTEST(boot_state, test_boot_s0)
{
	zassert_true(ap_power_in_state(AP_POWER_STATE_ON));

	/* AP must be left running, not power cycled. */
	k_msleep(100);
	zassert_true(ap_power_in_state(AP_POWER_STATE_ON));
	zassert_equal(1, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(1, power_signal_get(PWR_PCH_PWROK));
	zassert_equal(0, power_signal_get(PWR_EC_PCH_RSMRST));
	zassert_equal(0, power_shutdown_count);
	zassert_equal(0, power_hard_off_count);
}
#endif /* CONFIG_TEST_AP_POWER_BOOT_S0 */

#if defined(CONFIG_TEST_AP_POWER_BOOT_S3)
/*
 * Deliver the SLP_Sx wires some time after the EC starts, as happens when
 * the eSPI link has to be re-established after an EC reset. Each wire
 * arrives separately so the detection has to wait for all of them.
 */
static void send_s3_vw(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(send_s3_vw_work, send_s3_vw);

static void send_s3_vw(struct k_work *work)
{
	static int step;

	switch (step++) {
	case 0:
		send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S3, true);
		break;
	case 1:
		send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S4, false);
		break;
	default:
		send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S5, false);
		return;
	}
	k_work_reschedule(&send_s3_vw_work, K_MSEC(100));
}

ZTEST(boot_state_preset, test_preset_s3)
{
	/* eSPI reset asserted, so no VW values are known yet. */
	struct espi_event evt = {
		.evt_type = ESPI_BUS_RESET,
		.evt_data = 0,
	};

	preset_rails_on();
	preset_all_sys_pwrgd(0);

	zassert_ok(((const struct emul_espi_driver_api *)espi->api)
			   ->trigger_event(espi, &evt));
	zassert_equal(-EINVAL, power_signal_get(PWR_SLP_S3));
	zassert_equal(-EINVAL, power_signal_get(PWR_SLP_S4));
	zassert_equal(-EINVAL, power_signal_get(PWR_SLP_S5));

	k_work_reschedule(&send_s3_vw_work, K_MSEC(100));
}

ZTEST(boot_state, test_boot_s3)
{
	/*
	 * Detection must have waited for the SLP_Sx wires, otherwise the
	 * state is undeterminable and the AP is shut down.
	 */
	zassert_true(ap_power_in_state(AP_POWER_STATE_SUSPEND));

	k_msleep(500);
	zassert_true(ap_power_in_state(AP_POWER_STATE_SUSPEND));
	zassert_equal(1, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(0, power_signal_get(PWR_EC_PCH_RSMRST));
	/* Already suspended, so no S0 -> S3 transition is reported. */
	zassert_equal(0, power_suspend_count);
	zassert_equal(0, power_shutdown_count);
	zassert_equal(0, power_hard_off_count);
}
#endif /* CONFIG_TEST_AP_POWER_BOOT_S3 */

#if defined(CONFIG_TEST_AP_POWER_BOOT_S5)
ZTEST(boot_state_preset, test_preset_s5)
{
	preset_rails_on();
	preset_all_sys_pwrgd(0);
	send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S3, false);
	send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S4, true);
	send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S5, true);
}

ZTEST(boot_state, test_boot_s5)
{
	const struct gpio_dt_spec rsmrst_pwrgd =
		GPIO_DT_SPEC_GET(DT_NODELABEL(pwr_rsmrst_pwrgd), gpios);

	zassert_true(ap_power_in_state(AP_POWER_STATE_SOFT_OFF));
	zassert_equal(1, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(0, power_hard_off_count);

	/* Rails drop once the EC turns them off. */
	set_input(&rsmrst_pwrgd, 0);

	/* S5 inactivity timeout then drops the AP to G3. */
	k_msleep(AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout) * MSEC_PER_SEC +
		 500);
	zassert_true(ap_power_in_state(AP_POWER_STATE_HARD_OFF));
	zassert_equal(0, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(1, power_hard_off_count);
}
#endif /* CONFIG_TEST_AP_POWER_BOOT_S5 */

#if defined(CONFIG_TEST_AP_POWER_BOOT_UNKNOWN)
ZTEST(boot_state_preset, test_preset_unknown)
{
	/* Rails up but no sleep state asserted and system power not good. */
	preset_rails_on();
	preset_all_sys_pwrgd(0);
	send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S3, false);
	send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S4, false);
	send_slp_vw(ESPI_VWIRE_SIGNAL_SLP_S5, false);
}

ZTEST(boot_state, test_boot_unknown)
{
	/* The AP is forced off when its state can't be determined. */
	zassert_true(ap_power_in_state(AP_POWER_STATE_HARD_OFF));
	zassert_equal(0, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(1, power_signal_get(PWR_EC_PCH_RSMRST));
}
#endif /* CONFIG_TEST_AP_POWER_BOOT_UNKNOWN */

#if defined(CONFIG_TEST_AP_POWER_BOOT_RSMRST)
ZTEST(boot_state_preset, test_preset_rsmrst)
{
	const struct gpio_dt_spec rsmrst_pwrgd =
		GPIO_DT_SPEC_GET(DT_NODELABEL(pwr_rsmrst_pwrgd), gpios);
	/* The PCH is held in reset, so the eSPI link is down. */
	struct espi_event evt = {
		.evt_type = ESPI_BUS_RESET,
		.evt_data = 0,
	};

	/* Rails up, but RSMRST is still asserted to the PCH. */
	set_input(&rsmrst_pwrgd, 1);
	zassert_ok(power_signal_set(PWR_EN_PP3300_A, 1));
	zassert_equal(1, power_signal_get(PWR_EC_PCH_RSMRST));
	zassert_ok(((const struct emul_espi_driver_api *)espi->api)
			   ->trigger_event(espi, &evt));
}

ZTEST(boot_state, test_boot_rsmrst)
{
	/*
	 * The VW signals can't be read with the PCH in reset, so the AP is
	 * shut down without waiting for them.
	 */
	zassert_true(k_uptime_get() < 1000, "Boot waited for the VW signals");
	zassert_true(ap_power_in_state(AP_POWER_STATE_HARD_OFF));
	zassert_equal(0, power_signal_get(PWR_EN_PP3300_A));
	zassert_equal(1, power_signal_get(PWR_EC_PCH_RSMRST));
}
#endif /* CONFIG_TEST_AP_POWER_BOOT_RSMRST */

ZTEST_SUITE(boot_state_preset, ap_power_predicate_pre_main, NULL, NULL, NULL,
	    NULL);

ZTEST_SUITE(boot_state, ap_power_predicate_post_main, NULL, NULL, NULL, NULL);

/* These 2 lines are needed because we don't define an espi host driver */
#define DT_DRV_COMPAT zephyr_espi_emul_espi_host
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
