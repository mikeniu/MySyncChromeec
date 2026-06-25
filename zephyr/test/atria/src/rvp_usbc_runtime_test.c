/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Consolidated test suite for board_get_pdc_for_port() runtime detection.
 *
 * Uses reset_pdc_discovery_for_test() to reset internal state between tests,
 * allowing all hardware configurations to be tested in a single binary.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_realtek_rts54xx_public.h"
#include "emul/emul_tps6699x.h"
#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

/* Test-only function to reset discovery state between tests */
extern void reset_pdc_discovery_for_test(void);

/* ========================================================================== */
/* TI PDC Configuration Tests                                                 */
/* ========================================================================== */

ZTEST(rvp_usbc_runtime, test_three_port_ti)
{
	const struct device *dev;

	/* All three TI emulators respond, all RTK fail */
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c0))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c1))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c2))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c0))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	reset_pdc_discovery_for_test();

	/* THREE_TI detected, verify all ports */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c1)), dev);

	zassert_ok(board_get_pdc_for_port(2, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c2)), dev);

	/* Port 3 out of range */
	zassert_equal(-ENOENT, board_get_pdc_for_port(3, &dev));
	zassert_is_null(dev);
}

ZTEST(rvp_usbc_runtime, test_dual_port_ti)
{
	const struct device *dev;

	/* Only C0 and C1 TI respond, C2 fails */
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c0))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c1))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c0))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	reset_pdc_discovery_for_test();

	/* DUAL_TI detected, verify valid ports */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c1)), dev);

	/* Port 2 out of range in DUAL_TI */
	zassert_equal(-ENOENT, board_get_pdc_for_port(2, &dev));
	zassert_is_null(dev);
}

ZTEST(rvp_usbc_runtime, test_no_pdcs)
{
	const struct device *dev;

	/* All PDC probes fail */
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c0))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c0))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	reset_pdc_discovery_for_test();

	/* NONE detected, returns OK with dev=NULL */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_is_null(dev);
}

ZTEST(rvp_usbc_runtime, test_c0_only_ti)
{
	const struct device *dev;

	/* Only C0 TI responds, all others fail */
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c0))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c0))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	reset_pdc_discovery_for_test();

	/* C0_TI detected, verify C0 works and others fail */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0)), dev);

	zassert_equal(-ENOENT, board_get_pdc_for_port(1, &dev));
	zassert_is_null(dev);
}

ZTEST(rvp_usbc_runtime, test_unsupported_config)
{
	const struct device *dev;

	/* C0 and C2 TI respond, but not C1 (unsupported config) */
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c0))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c2))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c0))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	reset_pdc_discovery_for_test();

	/* Default case, all ports return -ENOENT */
	zassert_equal(-ENOENT, board_get_pdc_for_port(0, &dev));
	zassert_is_null(dev);
}

/* ========================================================================== */
/* RTK PDC Configuration Tests                                                */
/* ========================================================================== */

ZTEST(rvp_usbc_runtime, test_c0_only_rtk)
{
	const struct device *dev;

	/* Only C0 RTK responds, all others fail */
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c0))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c0))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	reset_pdc_discovery_for_test();

	/* C0_RTK detected, verify C0 works and others fail */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c0)), dev);

	zassert_equal(-ENOENT, board_get_pdc_for_port(1, &dev));
	zassert_is_null(dev);
}

ZTEST(rvp_usbc_runtime, test_dual_rtk)
{
	const struct device *dev;

	/* C0 and C1 RTK respond, all TI fail */
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c0))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c0))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c1))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	reset_pdc_discovery_for_test();

	/* DUAL_RTK detected, verify both ports */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c0)), dev);

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c1)), dev);

	/* Port 2 out of range */
	zassert_equal(-ENOENT, board_get_pdc_for_port(2, &dev));
	zassert_is_null(dev);
}

ZTEST(rvp_usbc_runtime, test_three_rtk)
{
	const struct device *dev;

	/* All three RTK emulators respond, all TI fail */
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c0))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c1))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_ti_c2))),
		I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c0))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c1))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		rts5453p_emul_get_i2c_common_data(
			EMUL_DT_GET(DT_NODELABEL(pdc_rtk_c2))),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	reset_pdc_discovery_for_test();

	/* THREE_RTK detected, verify all ports */
	zassert_ok(board_get_pdc_for_port(0, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c0)), dev);

	zassert_ok(board_get_pdc_for_port(1, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c1)), dev);

	zassert_ok(board_get_pdc_for_port(2, &dev));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c2)), dev);

	/* Port 3 out of range */
	zassert_equal(-ENOENT, board_get_pdc_for_port(3, &dev));
	zassert_is_null(dev);
}

ZTEST_SUITE(rvp_usbc_runtime, NULL, NULL, NULL, NULL, NULL);
