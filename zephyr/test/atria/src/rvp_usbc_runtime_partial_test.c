/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Shared test file for partial/edge-case hardware configurations.
 *
 * Each testcase.yaml entry compiles this file with a different overlay and
 * exactly one CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_* flag set, activating the
 * matching ZTEST block. The overlay controls which PDC chips are detected by
 * discover_tcss_modules(), which in turn drives which switch case in
 * board_get_pdc_for_port() is exercised.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_partial, NULL, NULL, NULL, NULL, NULL);

#if defined(CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_NONE)
/**
 * NONE: no PDCs detected → any port returns 0 with *dev = NULL.
 */
ZTEST(rvp_usbc_runtime_partial, test_none)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_is_null(dev);
}
#endif

#if defined(CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_C0_ONLY)
/**
 * C0_TI: port 0 succeeds, port 1 falls through to -ENOENT.
 */
ZTEST(rvp_usbc_runtime_partial, test_c0_only)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);

	zassert_equal(-ENOENT, board_get_pdc_for_port(1, &dev));
	zassert_is_null(dev);
}
#endif

#if defined(CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_UNSUPPORTED)
/**
 * Unsupported combo (C0 + C2, no C1) → default case, returns -ENOENT.
 */
ZTEST(rvp_usbc_runtime_partial, test_unsupported_combo)
{
	const struct device *dev;

	zassert_equal(-ENOENT, board_get_pdc_for_port(0, &dev));
	zassert_is_null(dev);
}
#endif
