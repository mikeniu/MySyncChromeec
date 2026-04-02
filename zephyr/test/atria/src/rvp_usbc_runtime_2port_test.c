/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * 2-port tests for board_get_pdc_for_port().
 *
 * Overlay: native_sim_2port.overlay — only C0 and C1 emulators present.
 * CONFIG_USB_PD_PORT_MAX_COUNT=2, so DEV_PDC_C2_TI=NULL and
 * probe_pdc_chip(NULL) returns false immediately (covers the NULL-dev guard).
 * Expected discovery result: RVP_TCSS_DUAL_TI.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(rvp_usbc_runtime_2port, NULL, NULL, NULL, NULL, NULL);

/**
 * First call triggers full discovery. C0 and C1 emulators respond; C2 is
 * NULL (USB_PD_PORT_MAX_COUNT=2) so probe_pdc_chip(NULL) returns false.
 * DUAL_TI is detected. Port 0 must return pdc_ti_c0.
 */
ZTEST(rvp_usbc_runtime_2port, test_discovery_and_port0)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);
}

/** Port 1 returns pdc_ti_c1 (uses cached DUAL_TI state). */
ZTEST(rvp_usbc_runtime_2port, test_port1)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c1)), dev);
}

/**
 * Port 2 has no match in the DUAL_TI switch case → falls through to
 * *dev = NULL, return -ENOENT. Also covers the switch break/fall-through path.
 */
ZTEST(rvp_usbc_runtime_2port, test_port_out_of_range)
{
	const struct device *dev;

	zassert_equal(-ENOENT, board_get_pdc_for_port(2, &dev));
	zassert_is_null(dev);
}

/**
 * Calling port 0 a second time confirms the ctx.initialized early-return
 * (caching) path is taken.
 */
ZTEST(rvp_usbc_runtime_2port, test_caching)
{
	const struct device *dev;

	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);
}
