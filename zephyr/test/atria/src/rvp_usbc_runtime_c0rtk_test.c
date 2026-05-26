/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * C0-only RTK test for board_get_pdc_for_port().
 *
 * Overlay: native_sim.overlay (ATRIA_C0RTK)
 * C0 RTK probe succeeds (tps6699 proxy); C1 RTK fake-absent; C2=NULL.
 * detected = RVP_TCSS_C0_RTK → C0_RTK switch case.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_c0rtk, NULL, NULL, NULL, NULL, NULL);

ZTEST(rvp_usbc_runtime_c0rtk, test_c0_rtk)
{
	const struct device *dev;

	/* First call triggers discovery → C0_RTK. */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c0)), dev);

	/* Port 1 has no match in the C0_RTK switch case → -ENOENT. */
	zassert_equal(-ENOENT, board_get_pdc_for_port(1, &dev));
	zassert_is_null(dev);
}
