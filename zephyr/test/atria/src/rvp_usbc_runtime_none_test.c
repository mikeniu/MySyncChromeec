/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * No-PDC test for board_get_pdc_for_port().
 *
 * Overlay: native_sim.overlay (CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_NONE=y)
 * Both C0 and C1 are fake-absent; C2=NULL.  All probes fail → NONE detected.
 * board_get_pdc_for_port() must return 0 with *dev = NULL for any port.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_none, NULL, NULL, NULL, NULL, NULL);

ZTEST(rvp_usbc_runtime_none, test_no_pdcs_detected)
{
	const struct device *dev;

	/* First call triggers discovery; all probes fail → NONE. */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_is_null(dev);
}
