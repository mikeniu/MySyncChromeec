/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accel_lis2dh.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_lis2dh.h"
#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>

#define LIS2DH_NODE DT_NODELABEL(lis2dh_emul)
#define ACC_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_lis2dh_accel))

static const struct emul *emul = EMUL_DT_GET(LIS2DH_NODE);
static struct motion_sensor_t *acc = &motion_sensors[ACC_SENSOR_ID];

static void lis2dh_before(void *state)
{
	ARG_UNUSED(state);
	lis2dh_emul_reset(emul);
}

ZTEST_USER(lis2dh, test_init)
{
	zassert_ok(lis2dh_drv.init(acc));
}

ZTEST(lis2dh, test_lis2dh_init__fail_read_who_am_i)
{
	struct i2c_common_emul_data *common_data =
		emul_lis2dh_get_i2c_common_data(emul);
	int rv;

	i2c_common_emul_set_read_fail_reg(common_data, LIS2DH_WHO_AM_I_REG);
	rv = lis2dh_drv.init(acc);
	zassert_equal(EC_ERROR_INVAL, rv);
}

ZTEST(lis2dh, test_lis2dh_init__fail_who_am_i)
{
	int rv;

	lis2dh_emul_set_who_am_i(emul, ~LIS2DH_WHO_AM_I);

	rv = lis2dh_drv.init(acc);
	zassert_equal(EC_ERROR_ACCESS_DENIED, rv,
		      "init returned %d but was expecting %d", rv,
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST(lis2dh, test_lis2dh__set_range)
{
	struct i2c_common_emul_data *common_data =
		emul_lis2dh_get_i2c_common_data(emul);
	int rv;

	zassert_ok(lis2dh_drv.init(acc));
	zassert_ok(lis2dh_drv.set_range(acc, 4, 0));
	printk("set_range: normalized range = %d\n", acc->current_range);
	zassert_equal(acc->current_range, 4);

	/* Configure LIS2DH_CTRL4_ADDR write to fail */
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(common_data, LIS2DH_CTRL4_ADDR);
	rv = lis2dh_drv.set_range(acc, 2, 0);
	zassert_equal(rv, EC_ERROR_INVAL);
	zassert_equal(acc->current_range, 4);

	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_ok(lis2dh_drv.set_range(acc, 2, 0));
	zassert_equal(acc->current_range, 2);
}

ZTEST_USER(lis2dh, test_lis2dh__set_data_rate)
{
	struct stprivate_data *drvdata = acc->drv_data;

	zassert_ok(lis2dh_drv.init(acc));

	/* Set data rate to 10Hz */
	zassert_ok(acc->drv->set_data_rate(acc, 10000, 1));
	zassert_equal(drvdata->base.odr, 10000);

	/* Set data rate to 0 (Power Off) */
	zassert_ok(acc->drv->set_data_rate(acc, 0, 1));
	zassert_equal(drvdata->base.odr, 0);
}

ZTEST_SUITE(lis2dh, drivers_predicate_post_main, NULL, lis2dh_before, NULL,
	    NULL);
