/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * 3-port test for board_get_pdc_for_port().
 *
 * Overlay: native_sim.overlay (CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_3PORT=y)
 * All three TPS6699x emulators respond → THREE_TI detected.
 *
 * A single ZTEST covers every port query so that the first call always
 * triggers a fresh discover_tcss_modules() run (ctx starts zero-initialised
 * in each test binary).
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_3port, NULL, NULL, NULL, NULL, NULL);

/**
 * First call triggers discovery; all three emulators respond so THREE_TI is
 * detected.  Verify each valid port and confirm an out-of-range port returns
 * -ENOENT.
 */
ZTEST(rvp_usbc_runtime_3port, test_all_ports)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c1)), dev);

	zassert_ok(board_get_pdc_for_port(2, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c2)), dev);

	/* Port 3 has no match in the THREE_TI switch case. */
	zassert_equal(-ENOENT, board_get_pdc_for_port(3, &dev));
	zassert_is_null(dev);
}
