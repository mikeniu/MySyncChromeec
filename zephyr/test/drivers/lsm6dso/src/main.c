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

ZTEST(lsm6dso, test_push_fifo_data_oob_tags)
{
	/* Set FIFO length to 1 */
	uint16_t sts = 1;
	zassert_ok(i2c_write16(acc->port, acc->i2c_spi_addr_flags,
			       LSM6DSO_FIFO_STS1_ADDR, sts));

	/* Set invalid tag (0xFF >> 3 is 31, GYRO_TAG is 1) */
	zassert_ok(i2c_write8(acc->port, acc->i2c_spi_addr_flags,
			      LSM6DSO_FIFO_DATA_ADDR_TAG, 0xFF));

	uint32_t event = CONFIG_ACCEL_LSM6DSO_INT_EVENT;
	/* It should consume the event and not crash */
	zassert_ok(acc->drv->irq_handler(acc, &event));
}

ZTEST(lsm6dso, test_set_range_rounding_clamping)
{
	/* Test accel clamping to max */
	zassert_ok(acc->drv->set_range(acc, LSM6DSO_ACCEL_FS_MAX_VAL * 2, 0));
	zassert_equal(acc->current_range, LSM6DSO_ACCEL_FS_MAX_VAL);

	/* Test accel rounding up */
	zassert_ok(acc->drv->set_range(acc, LSM6DSO_ACCEL_FS_MIN_VAL + 1, 1));
	/* Should round up to the next power of 2 */
	zassert_equal(acc->current_range, LSM6DSO_ACCEL_FS_MIN_VAL * 2);

	/* Test gyro clamping to max */
	struct motion_sensor_t *gyro = &motion_sensors[GYR_SENSOR_ID];
	zassert_ok(gyro->drv->set_range(gyro, 10000000, 0));

	/* Test gyro rounding up */
	zassert_ok(gyro->drv->set_range(gyro, LSM6DSO_GYRO_NORMALIZE_FS(0) + 1,
					1));
	zassert_equal(gyro->current_range, LSM6DSO_GYRO_NORMALIZE_FS(1));
}
