/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * C0-only test for board_get_pdc_for_port().
 *
 * Overlay: native_sim.overlay (CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_C0_ONLY=y)
 * C0 responds; C1 is fake-absent; C2=NULL.  Probes: C0=true, C1=false
 * → RVP_TCSS_C0_TI detected.  Port 0 succeeds; port 1 falls through to
 * -ENOENT.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_c0only, NULL, NULL, NULL, NULL, NULL);

ZTEST(rvp_usbc_runtime_c0only, test_c0_only)
{
	const struct device *dev;

	/* First call triggers discovery → C0_TI. */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);

	/* Port 1 has no match in the C0_TI switch case. */
	zassert_equal(-ENOENT, board_get_pdc_for_port(1, &dev));
	zassert_is_null(dev);
}
