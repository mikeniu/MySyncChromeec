/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * 2-port test for board_get_pdc_for_port().
 *
 * Overlay: native_sim.overlay (ATRIA_2PORT)
 * Only C0 and C1 emulators present; USB_PD_PORT_MAX_COUNT=2 so DEV_PDC_C2_TI
 * is NULL and probe_pdc_chip(NULL) returns false immediately.
 * Expected discovery result: DUAL_TI.
 *
 * A single ZTEST covers all port queries so the first call always triggers a
 * fresh discover_tcss_modules() run.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_2port, NULL, NULL, NULL, NULL, NULL);

/**
 * First call triggers discovery; C2=NULL so probe_pdc_chip(NULL) returns
 * false immediately.  DUAL_TI is detected.  Both valid ports are verified,
 * then an out-of-range port exercises the DUAL_TI switch break.
 */
ZTEST(rvp_usbc_runtime_2port, test_all_ports)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c1)), dev);

	/* Port 2 has no match in the DUAL_TI switch case → -ENOENT. */
	zassert_equal(-ENOENT, board_get_pdc_for_port(2, &dev));
	zassert_is_null(dev);
}
