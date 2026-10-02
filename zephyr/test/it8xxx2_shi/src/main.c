/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>

#define SHI_NODE DT_NODELABEL(shi0)

static const struct device *const shi_dev = DEVICE_DT_GET(SHI_NODE);

static int test_shi_pm_action(const struct device *dev,
			      enum pm_device_action action)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(action);

	return 0;
}

PM_DEVICE_DT_DEFINE(SHI_NODE, test_shi_pm_action);
DEVICE_DT_DEFINE(SHI_NODE, NULL, PM_DEVICE_DT_GET(SHI_NODE), NULL, NULL,
		 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

static void *it8xxx2_shi_setup(void)
{
	zassert_true(device_is_ready(shi_dev));
	zassert_ok(pm_device_runtime_enable(shi_dev));
	zassert_true(pm_device_runtime_is_enabled(shi_dev));
	zassert_equal(pm_device_runtime_usage(shi_dev), 0);

	return NULL;
}

static void it8xxx2_shi_before(void *fixture)
{
	int usage;

	ARG_UNUSED(fixture);

	usage = pm_device_runtime_usage(shi_dev);
	while (usage > 0) {
		zassert_ok(pm_device_runtime_put(shi_dev));
		usage = pm_device_runtime_usage(shi_dev);
	}
	zassert_equal(usage, 0);
	disable_sleep(SLEEP_MASK_SPI);
}

ZTEST_SUITE(it8xxx2_shi, NULL, it8xxx2_shi_setup, it8xxx2_shi_before, NULL,
	    NULL);

ZTEST(it8xxx2_shi, test_pre_init_resumes_shi)
{
	enum pm_device_state state;

	ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);

	zassert_equal(pm_device_runtime_usage(shi_dev), 1);
	zassert_ok(pm_device_state_get(shi_dev, &state));
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE);
}

ZTEST(it8xxx2_shi, test_shutdown_complete_allows_deep_sleep)
{
	zassert_not_equal(atomic_get(&sleep_mask) & SLEEP_MASK_SPI, 0);

	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN_COMPLETE);

	zassert_equal(atomic_get(&sleep_mask) & SLEEP_MASK_SPI, 0);
}

ZTEST(it8xxx2_shi, test_hard_off_suspends_shi)
{
	enum pm_device_state state;

	ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);

	zassert_equal(pm_device_runtime_usage(shi_dev), 0);
	zassert_ok(pm_device_state_get(shi_dev, &state));
	zassert_equal(state, PM_DEVICE_STATE_SUSPENDED);
}
