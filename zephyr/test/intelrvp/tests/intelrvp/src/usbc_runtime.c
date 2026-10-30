/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <zephyr/device.h>
#include <zephyr/ztest.h>

ZTEST(pdc_runtime_port_config, test_board_get_pdc_for_port)
{
	const struct device *dev = NULL;

#if !defined(CONFIG_EMUL_PDC_RTS54) && !defined(CONFIG_EMUL_PDC_TPS6699X)
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_is_null(dev);
#elif defined(CONFIG_EMUL_PDC_TPS6699X) && !defined(CONFIG_EMUL_PDC_RTS54)
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(dev, DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)));

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(dev, DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c1)));

	zassert_equal(board_get_pdc_for_port(99, &dev), -ENOENT);
	zassert_is_null(dev);
#elif defined(CONFIG_EMUL_PDC_RTS54) && !defined(CONFIG_EMUL_PDC_TPS6699X)
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(dev, DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c0)));

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(dev, DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c1)));

	zassert_equal(board_get_pdc_for_port(99, &dev), -ENOENT);
	zassert_is_null(dev);
#endif
}

ZTEST_SUITE(pdc_runtime_port_config, NULL, NULL, NULL, NULL, NULL);
