/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "power_signals.h"
#include "test_state.h"

#include <zephyr/ztest.h>

/**
 * @brief TestPurpose: Watchdog warning snapshots AP power state/signals
 *
 * Expected Results
 *  - The watchdog struct records the cached power signal mask.
 *  - The power state maps to a valid enum panic_power_state value.
 */
ZTEST(panic_watchdog, test_watchdog_warning_power_snapshot)
{
	struct panic_data *pdata;

	panic_data_write_watchdog_warning(0, 0, 0);
	pdata = panic_get_data();

	zassert_not_null(pdata);
	zassert_equal(pdata->arch, PANIC_ARCH_WATCHDOG);
	zassert_equal(pdata->wd.power_signals, power_get_signals_cached());
	zassert_true(pdata->wd.power_state <= PANIC_POWER_S0S0IX,
		     "Unexpected power_state %u", pdata->wd.power_state);
}

ZTEST_SUITE(panic_watchdog, ap_power_predicate_post_main, NULL, NULL, NULL,
	    NULL);
