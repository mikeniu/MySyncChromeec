/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Three-RTK test for board_get_pdc_for_port().
 *
 * Overlay: native_sim.overlay (ATRIA_THREE_RTK)
 * All three RTK probes succeed (tps6699 proxies); all TI nodes fake-absent.
 * detected = RVP_TCSS_THREE_RTK → THREE_RTK switch case.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_three_rtk, NULL, NULL, NULL, NULL, NULL);

ZTEST(rvp_usbc_runtime_three_rtk, test_three_rtk)
{
	const struct device *dev;

	/* First call triggers discovery → THREE_RTK. */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c0)), dev);

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c1)), dev);

	zassert_ok(board_get_pdc_for_port(2, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c2)), dev);

	/* Port 3 has no match in the THREE_RTK switch case → -ENOENT. */
	zassert_equal(-ENOENT, board_get_pdc_for_port(3, &dev));
	zassert_is_null(dev);
}
