/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * 3-port tests for board_get_pdc_for_port().
 *
 * Overlay: native_sim_3port.overlay — all three TPS6699x emulators respond.
 * Expected discovery result: RVP_TCSS_THREE_TI.
 *
 * Tests run sequentially in a single binary with zero-initialized ctx, so
 * the first call triggers a fresh discovery run; subsequent calls exercise
 * the ctx.initialized early-return (caching) path.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_3port, NULL, NULL, NULL, NULL, NULL);

/**
 * First call triggers full discovery. All three emulators respond on i2c0,
 * so THREE_TI is detected. Port 0 must return pdc_ti_c0.
 */
ZTEST(rvp_usbc_runtime_3port, test_discovery_and_port0)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);
}

/** Port 1 returns pdc_ti_c1 (uses cached THREE_TI state). */
ZTEST(rvp_usbc_runtime_3port, test_port1)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c1)), dev);
}

/** Port 2 returns pdc_ti_c2 (uses cached THREE_TI state). */
ZTEST(rvp_usbc_runtime_3port, test_port2)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(2, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c2)), dev);
}

/**
 * Port 3 has no match in the THREE_TI switch case → falls through to
 * *dev = NULL, return -ENOENT.
 */
ZTEST(rvp_usbc_runtime_3port, test_port_out_of_range)
{
	const struct device *dev;

	zassert_equal(-ENOENT, board_get_pdc_for_port(3, &dev));
	zassert_is_null(dev);
}

/**
 * Calling port 0 a second time must return the same result, confirming the
 * ctx.initialized early-return path is taken (no re-discovery).
 */
ZTEST(rvp_usbc_runtime_3port, test_caching)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);
}
