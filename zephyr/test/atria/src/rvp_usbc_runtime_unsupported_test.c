/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Unsupported-combo test for board_get_pdc_for_port().
 *
 * Overlay: native_sim.overlay
 * (CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_UNSUPPORTED=y)
 * C0 and C2 respond; C1 is fake-absent.
 * detected = RVP_TCSS_C0_TI | RVP_TCSS_C2_TI → falls to default case in the
 * switch, returning -ENOENT for all ports.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_unsupported, NULL, NULL, NULL, NULL, NULL);

ZTEST(rvp_usbc_runtime_unsupported, test_unsupported_combo)
{
	const struct device *dev;

	/* First call triggers discovery; unsupported combo hits default case.
	 */
	zassert_equal(-ENOENT, board_get_pdc_for_port(0, &dev));
	zassert_is_null(dev);
}
