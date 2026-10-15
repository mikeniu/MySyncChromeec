/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "driver/accelgyro_lsm6dso.h"
#include "driver/accelgyro_lsm6dso_public.h"
#include "emul/emul_lsm6dso.h"
#include "i2c.h"
#include "motion_sense.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>

#define LSM6DSO_NODE DT_NODELABEL(lsm6dso_emul)
#define ACC_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_lsm6dso_accel))
#define GYR_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_lsm6dso_gyro))

static const struct emul *emul = EMUL_DT_GET(LSM6DSO_NODE);
static struct motion_sensor_t *acc = &motion_sensors[ACC_SENSOR_ID];

ZTEST_SUITE(lsm6dso, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST(lsm6dso, test_accelgyro_config_fifo_power_off)
{
	/* Set odr to 0 to trigger the power-off condition in
	 * accelgyro_config_fifo */
	zassert_ok(acc->drv->set_data_rate(acc, 0, 0));

	/* Verify the ODR is set to 0 */
	zassert_equal(acc->drv->get_data_rate(acc), 0);

	/* In accelgyro_config_fifo, LSM6DSO_FIFO_CTRL3_ADDR should be written
	 * with LSM6DSO_DIS_BIT (which is 0) when odr is 0.
	 * We can verify this via the emulator by peeking.
	 */
	int ctrl3 = 0xFF;
	zassert_ok(i2c_read8(acc->port, acc->i2c_spi_addr_flags,
			     LSM6DSO_FIFO_CTRL3_ADDR, &ctrl3));
	/* 0x00 is LSM6DSO_DIS_BIT */
	zassert_equal(ctrl3 & LSM6DSO_FIFO_ODR_MASK(acc), 0);
}

ZTEST(lsm6dso, test_set_data_rate_fifo_error)
{
	/* Set read/write fail on LSM6DSO_FIFO_CTRL3_ADDR to simulate
	 * accelgyro_config_fifo failure */
	lsm6dso_emul_set_write_fail_reg(emul, LSM6DSO_FIFO_CTRL3_ADDR);

	/* Call set_data_rate, it should propagate the error */
	zassert_equal(EC_ERROR_INVAL, acc->drv->set_data_rate(acc, 13000, 1));

	/* Clear the failure */
	lsm6dso_emul_set_write_fail_reg(emul, -1);
}
