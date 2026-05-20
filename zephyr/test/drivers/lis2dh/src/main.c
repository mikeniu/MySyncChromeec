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
	const struct gpio_dt_spec spec =
		GPIO_DT_SPEC_GET(LIS2DH_NODE, int_gpios);

	printk("int_gpio = %d\n", gpio_pin_get_dt(&spec));
	/* Basic initialization works. */
	zassert_ok(lis2dh_drv.init(acc));
	printk("finished test_init\n");
}

ZTEST(lis2dh, test_lis2dh_init__fail_read_who_am_i)
{
	struct i2c_common_emul_data *common_data =
		emul_lis2dh_get_i2c_common_data(emul);
	int rv;

	i2c_common_emul_set_read_fail_reg(common_data, LIS2DH_WHO_AM_I_REG);
	rv = lis2dh_drv.init(acc);
	printk("finished test_init fail who i am read\n");
	zassert_equal(EC_ERROR_INVAL, rv);
}

ZTEST(lis2dh, test_lis2dh_init__fail_who_am_i)
{
	int rv;

	lis2dh_emul_set_who_am_i(emul, ~LIS2DH_WHO_AM_I);

	rv = lis2dh_drv.init(acc);
	printk("finished wrong who I am reg\n");
	zassert_equal(EC_ERROR_ACCESS_DENIED, rv,
		      "init returned %d but was expecting %d", rv,
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST_USER(lis2dh, test_fifo_read)
{
	struct stprivate_data *drvdata = acc->drv_data;
	intv3_t fake_sample = { 100, 200, 300 };
	uint32_t evt = ACCEL_LIS2DH_INT_EVENT;
	const struct gpio_dt_spec spec =
		GPIO_DT_SPEC_GET(LIS2DH_NODE, int_gpios);

	printk("int_gpio = %d\n", gpio_pin_get_dt(&spec));

	/* Basic initialization works. */
	zassert_ok(lis2dh_drv.init(acc));

	/* Verify fifo mode is enabled */
	zassert_true(lis2dh_emul_is_fifo_enabled(emul));

	/* Reading requires a range to be set. Use 1 so it has no effect
	 * when scaling samples. Also need to set the sensor resolution
	 * manually.
	 */
	lis2dh_drv.set_range(acc, 1, 0);
	drvdata->resol = LIS2DH_RESOLUTION;

	lis2dh_emul_set_fifo_data(emul, (const uint8_t *)fake_sample,
				  sizeof(fake_sample));

	/* Run interrupt handler */
	zassert_ok(lis2dh_drv.irq_handler(acc, &evt));
}

ZTEST_SUITE(lis2dh, drivers_predicate_post_main, NULL, lis2dh_before, NULL,
	    NULL);
