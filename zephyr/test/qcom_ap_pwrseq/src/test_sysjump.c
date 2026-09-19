/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_interface.h"
#include "chipset.h"
#include "hooks.h"
#include "mock_chipset.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/init.h>
#include <zephyr/ztest.h>

#define AP_PWRSEQ_NODE DT_NODELABEL(qcom_ap_pwrseq)

static const struct device *const ap_pwrseq_dev = DEVICE_DT_GET(AP_PWRSEQ_NODE);
static const struct gpio_dt_spec ap_suspend_gpio =
	GPIO_DT_SPEC_GET(AP_PWRSEQ_NODE, ap_suspend_gpios);
static const struct gpio_dt_spec power_good_gpio =
	GPIO_DT_SPEC_GET(AP_PWRSEQ_NODE, power_good_gpios);

#if defined(CONFIG_TEST_SYSJUMP_S0)
static const int power_good_init_val = 1;
static const int ap_suspend_init_val = 0;
static const enum chipset_state_mask expected_state = CHIPSET_STATE_ON;
static const bool expected_sleep_disabled = true;
#elif defined(CONFIG_TEST_SYSJUMP_S3)
static const int power_good_init_val = 1;
static const int ap_suspend_init_val = 1;
static const enum chipset_state_mask expected_state = CHIPSET_STATE_SUSPEND;
static const bool expected_sleep_disabled = false;
#elif defined(CONFIG_TEST_SYSJUMP_G3)
static const int power_good_init_val = 0;
static const int ap_suspend_init_val = 0;
static const enum chipset_state_mask expected_state = CHIPSET_STATE_HARD_OFF;
static const bool expected_sleep_disabled = false;
#endif

#define TEST_PRE_INIT_PRIO 80
BUILD_ASSERT(TEST_PRE_INIT_PRIO > CONFIG_GPIO_INIT_PRIORITY &&
	     TEST_PRE_INIT_PRIO < CONFIG_APPLICATION_INIT_PRIORITY);

static int sysjump_pre_init(void)
{
	system_set_reset_flags(EC_RESET_FLAG_SYSJUMP);
	gpio_pin_configure_dt(&power_good_gpio, GPIO_INPUT);
	gpio_pin_configure_dt(&ap_suspend_gpio, GPIO_INPUT);
	gpio_emul_input_set(power_good_gpio.port, power_good_gpio.pin,
			    power_good_init_val);
	gpio_emul_input_set(ap_suspend_gpio.port, ap_suspend_gpio.pin,
			    ap_suspend_init_val);
	return 0;
}
SYS_INIT(sysjump_pre_init, POST_KERNEL, TEST_PRE_INIT_PRIO);

static void verify_sysjump_init(void)
{
	zassert_true(device_is_ready(ap_pwrseq_dev));
	zassert_true(chipset_in_state(expected_state));
	zassert_equal(!!(atomic_get(&sleep_mask) & SLEEP_MASK_AP_RUN),
		      expected_sleep_disabled);

	ap_pwrseq_task_start();
	zassert_true(chipset_in_state(expected_state));
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_PRE_INIT), 0);
	zassert_equal(mock_get_hook_notify_count(HOOK_CHIPSET_STARTUP), 0);
}

/*
 * Verify sysjump initialization across AP power states:
 * - S0 (power_good=1, ap_suspend=0): initializes in S0 with AP sleep disabled.
 * - S3 (power_good=1, ap_suspend=1): initializes in S3 with AP sleep enabled.
 * - G3 (power_good=0, ap_suspend=0): initializes in G3 with AP sleep enabled.
 *
 * 1. Check that the driver initializes in the expected state with the matching
 *    AP sleep mask configuration.
 * 2. Start the power sequence task and verify the chipset remains in the
 *    initial state without triggering a new startup sequence or auto power-on.
 */
ZTEST(qcom_ap_pwrseq, test_init_sysjump)
{
	verify_sysjump_init();
}

ZTEST_SUITE(qcom_ap_pwrseq, NULL, NULL, NULL, NULL, NULL);
