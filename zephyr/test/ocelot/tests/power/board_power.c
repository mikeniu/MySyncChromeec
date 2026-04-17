/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fakes.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include <ap_power/ap_power_interface.h>
#include <ap_power/ap_pwrseq.h>
#include <ap_power/ap_pwrseq_sm.h>
#include <ap_power_override_functions.h>
#include <mock/ap_power_events.h>
#include <mock/power_signals.h>
#include <power_signals.h>
#include <x86_power_signals.h>

#define X86_NON_DSX_FORCE_SHUTDOWN_TO_MS 50

int mock_power_signal_set_ap_force_shutdown(enum power_signal signal, int value)
{
	if (power_signal_set_fake.call_count == 1) {
		zassert_true(signal == PWR_PCH_PWROK && value == 0,
			     "First call signal: %d, value: %d", signal, value);
		return 0;
	} else if (power_signal_set_fake.call_count == 2) {
		zassert_true(signal == PWR_EC_PCH_SYS_PWROK && value == 0,
			     "Second call signal: %d, value: %d", signal,
			     value);
		return 0;
	} else if (power_signal_set_fake.call_count == 3) {
		zassert_true(signal == PWR_EC_PCH_RSMRST && value == 1,
			     "Third call signal: %d, value: %d", signal, value);
		return 0;
	} else if (power_signal_set_fake.call_count == 4) {
		zassert_true(signal == PWR_EN_PP3300_A && value == 0,
			     "Fourth call signal: %d, value: %d", signal,
			     value);
		return 0;
	}

	zassert_unreachable(
		"Wrong input received. power_signal_set_fake.call_count: %d, "
		"signal: %d, value: %d",
		power_signal_set_fake.call_count, signal, value);
	return -1;
}

int mock_power_signal_get_ap_force_shutdown(enum power_signal signal)
{
	if (signal == PWR_RSMRST_PWRGD) {
		if (power_signal_get_fake.call_count <= 5) {
			return 1;
		}
		return 0;
	}
	zassert_unreachable("Wrong input received. signal: %d", signal);
	return -1;
}

int mock_power_signal_get_ap_force_shutdown_retries(enum power_signal signal)
{
	if (signal == PWR_RSMRST_PWRGD) {
		return 1;
	}

	zassert_unreachable("Wrong input received. signal: %d", signal);
	return -1;
}

static int chipset_run_count;

int mock_power_signal_set_ap_power_action_g3_run_1(enum power_signal signal,
						   int value)
{
	if ((signal == PWR_EN_PP3300_A) && (value == 1)) {
		return 0;
	}

	zassert_unreachable("Wrong input received. signal: %d, value: %d",
			    signal, value);
	return -1;
}

int mock_power_signal_get_ap_power_action_g3_run_1(enum power_signal signal)
{
	if (signal == PWR_EN_PP3300_A) {
		return 1;
	}
	zassert_unreachable("Wrong input received. signal: %d", signal);
	return -1;
}

int mock_power_signal_get_ap_power_action_g3_run_0(enum power_signal signal)
{
	if (signal == PWR_EN_PP3300_A) {
		return 0;
	}
	zassert_unreachable("Wrong input received. signal: %d", signal);
	return -1;
}

/**
 * This will help to verify that function `board_ap_power_action_g3_run` is
 * returning proper value by evaluating power signals.
 * This function will only be called when `board_ap_power_action_g3_run`
 * returns 0 (zero).
 **/
static int chipset_ap_power_action_g3_run(void *data)
{
	chipset_run_count++;

	return 0;
}

AP_POWER_CHIPSET_STATE_DEFINE(G3, NULL, chipset_ap_power_action_g3_run, NULL);

#if CONFIG_AP_PWRSEQ_S0IX
AP_POWER_CHIPSET_SUB_STATE_DEFINE(S0ix, NULL, x86_non_dsx_mtl_s0ix_run, NULL,
				  S0);
#endif /* CONFIG_AP_PWRSEQ_S0IX */

static void board_power_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(power_signal_set);
	RESET_FAKE(power_signal_get);
	RESET_FAKE(power_wait_mask_signals_timeout);
	RESET_FAKE(ap_power_ev_send_callbacks);
	chipset_run_count = 0;
}

ZTEST_USER(board_power, test_board_ap_power_force_shutdown)
{
	power_signal_set_fake.custom_fake =
		mock_power_signal_set_ap_force_shutdown;
	power_signal_get_fake.custom_fake =
		mock_power_signal_get_ap_force_shutdown;
	board_ap_power_force_shutdown();

	zassert_equal(4, power_signal_set_fake.call_count);
	zassert_equal(7, power_signal_get_fake.call_count);
}

ZTEST_USER(board_power, test_board_ap_power_force_shutdown_timeout)
{
	power_signal_set_fake.custom_fake =
		mock_power_signal_set_ap_force_shutdown;
	power_signal_get_fake.custom_fake =
		mock_power_signal_get_ap_force_shutdown_retries;

	const uint32_t start_ms = k_uptime_get();

	board_ap_power_force_shutdown();

	const uint32_t end_ms = k_uptime_get();

	zassert_true((end_ms - start_ms) >= X86_NON_DSX_FORCE_SHUTDOWN_TO_MS);
	zassert_equal(power_signal_set_fake.call_count, 4);
	zassert_true(power_signal_get_fake.call_count > 2);
}

ZTEST_USER(board_power, test_board_ap_power_action_g3_run_0)
{
	const struct device *dev = ap_pwrseq_get_instance();

	ap_pwrseq_start(dev, AP_POWER_STATE_G3);

	power_signal_get_fake.custom_fake =
		mock_power_signal_get_ap_power_action_g3_run_0;

	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_SIGNAL);
	/* Buffer time to process event */
	k_msleep(5);
	zassert_equal(0, chipset_run_count);
}

ZTEST_USER(board_power, test_board_ap_power_action_g3_run_1)
{
	const struct device *dev = ap_pwrseq_get_instance();

	power_signal_set_fake.custom_fake =
		mock_power_signal_set_ap_power_action_g3_run_1;
	power_signal_get_fake.custom_fake =
		mock_power_signal_get_ap_power_action_g3_run_0;

	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_STARTUP);
	/* Buffer time to process event */
	k_msleep(5);
	zassert_equal(0, chipset_run_count);
}

ZTEST_USER(board_power, test_board_ap_power_action_g3_run_2)
{
	const struct device *dev = ap_pwrseq_get_instance();

	power_signal_set_fake.custom_fake =
		mock_power_signal_set_ap_power_action_g3_run_1;
	power_signal_get_fake.custom_fake =
		mock_power_signal_get_ap_power_action_g3_run_1;

	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_STARTUP);
	/* Buffer time to process event */
	k_msleep(5);
	zassert_equal(1, chipset_run_count);
}

ZTEST_SUITE(board_power, NULL, NULL, board_power_before, NULL, NULL);
