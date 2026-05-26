/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Dual-RTK test for board_get_pdc_for_port().
 *
 * Overlay: native_sim.overlay (ATRIA_DUAL_RTK)
 * C0 and C1 RTK probes succeed; C2=NULL (USB_PD_PORT_MAX_COUNT=2).
 * detected = RVP_TCSS_DUAL_RTK → DUAL_RTK switch case.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_dual_rtk, NULL, NULL, NULL, NULL, NULL);

ZTEST(rvp_usbc_runtime_dual_rtk, test_dual_rtk)
{
	const struct device *dev;

	/* First call triggers discovery → DUAL_RTK. */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c0)), dev);

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c1)), dev);

	/* Port 2 has no match in the DUAL_RTK switch case → -ENOENT. */
	zassert_equal(-ENOENT, board_get_pdc_for_port(2, &dev));
	zassert_is_null(dev);
}
