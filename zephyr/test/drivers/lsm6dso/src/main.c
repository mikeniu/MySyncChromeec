/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accelgyro_lsm6dso.h"
#include "driver/accelgyro_lsm6dso_public.h"
#include "emul/emul_lsm6dso.h"
#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>

#define LSM6DSO_NODE DT_NODELABEL(lsm6dso_emul)
#define ACC_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_lsm6dso_accel))
#define GYR_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_lsm6dso_gyro))

static const struct emul *emul = EMUL_DT_GET(LSM6DSO_NODE);
static struct motion_sensor_t *acc = &motion_sensors[ACC_SENSOR_ID];
static struct motion_sensor_t *gyr = &motion_sensors[GYR_SENSOR_ID];

ZTEST_SUITE(lsm6dso, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST(lsm6dso, test_disable_interrupt)
{
	static struct ec_response_motion_sensor_data data[2];

	const struct gpio_dt_spec spec =
		GPIO_DT_SPEC_GET(LSM6DSO_NODE, int_gpios);
	int count;
	uint16_t read_byte_count;

	/* Set data rate to 13Hz, without this the FIFO will never be enabled */
	acc->config[SENSOR_CONFIG_AP].odr = LSM6DSO_ODR_MIN_VAL;
	acc->drv->set_data_rate(acc, LSM6DSO_ODR_MIN_VAL, 1);
	k_msleep(100);

	/* The lsm6dso throws away a few samples after an ODR change, burn
	 * through those so that the next sample would get on the FIFO if
	 * enabled.
	 */
	for (int i = 0; i < LSM6DSO_DISCARD_SAMPLES; ++i) {
		emul_lsm6dso_append_sample(emul, MOTIONSENSE_TYPE_ACCEL, 0.0f,
					   0.0f, 0.0f);
	}
	k_msleep(100);

	/* Disable the interrupts */
	acc->drv->enable_interrupt(acc, false);

	/* Clear out the soft motionsense FIFO */
	motion_sense_fifo_reset();

	/* Add an accel sample while interrupts are disabled */
	emul_lsm6dso_append_sample(emul, MOTIONSENSE_TYPE_ACCEL, 0.5f, -0.5f,
				   0.25f);
	k_msleep(100);

	/* Interrupt should be deasserted and no events should be on the FIFO */
	count = motion_sense_fifo_read(sizeof(data), ARRAY_SIZE(data), data,
				       &read_byte_count);
	zassert_equal(0, gpio_pin_get_dt(&spec));
	zassert_equal(0, count, "FIFO had %d entries", count);
	zassert_equal(0, read_byte_count, "FIFO read %u bytes",
		      read_byte_count);

	/* Flush the hardware FIFO and reset the soft motionsense FIFO */
	uint32_t event = CONFIG_ACCEL_LSM6DSO_INT_EVENT;
	zassert_equal(EC_SUCCESS, acc->drv->irq_handler(acc, &event));
	motion_sense_fifo_reset();

	/* Enable the interrupts */
	acc->drv->enable_interrupt(acc, true);

	/* Add another accel sample (1g, 0.5g, -0.25g) */
	emul_lsm6dso_append_sample(emul, MOTIONSENSE_TYPE_ACCEL, 1.0f, 0.5f,
				   -0.25f);
	zassert_equal(1, gpio_pin_get_dt(&spec));

	/* After we sleep, the motion sense loop should read the sample */
	k_msleep(100);
	count = motion_sense_fifo_read(sizeof(data), ARRAY_SIZE(data), data,
				       &read_byte_count);
	zassert_equal(0, gpio_pin_get_dt(&spec));
	zassert_equal(2, count, "FIFO had %d entries", count);
	zassert_equal(sizeof(data), read_byte_count, "FIFO read %u bytes",
		      read_byte_count);
	zassert_equal(MOTIONSENSE_SENSOR_FLAG_TIMESTAMP, data[0].flags);
	zassert_equal(ACC_SENSOR_ID, data[0].sensor_num);
	zassert_equal(0, data[1].flags);
	zassert_equal(ACC_SENSOR_ID, data[1].sensor_num,
		      "Sensor # expected %d, was %u", ACC_SENSOR_ID,
		      data[1].sensor_num);
	zassert_within(INT16_MAX / acc->current_range, data[1].data[X], 1);
	zassert_within(INT16_MAX / (2 * acc->current_range), data[1].data[Y],
		       1);
	zassert_within(-INT16_MAX / (4 * acc->current_range), data[1].data[Z],
		       1);
}

ZTEST(lsm6dso, test_gyro_sample)
{
	static struct ec_response_motion_sensor_data data[2];
	int count;
	uint16_t read_byte_count;

	/* Configure Gyro ODR and burn discard samples */
	gyr->config[SENSOR_CONFIG_AP].odr = LSM6DSO_ODR_MIN_VAL;
	gyr->drv->set_data_rate(gyr, LSM6DSO_ODR_MIN_VAL, 1);
	gyr->oversampling_ratio = 1;
	k_msleep(100);
	for (int i = 0; i < LSM6DSO_DISCARD_SAMPLES; ++i) {
		emul_lsm6dso_append_sample(emul, MOTIONSENSE_TYPE_GYRO, 0.0f,
					   0.0f, 0.0f);
	}
	k_msleep(100);
	motion_sense_fifo_reset();

	/* Enable interrupts */
	acc->drv->enable_interrupt(acc, true);

	/* Append gyro sample (100 dps, 50 dps, -25 dps) */
	emul_lsm6dso_append_sample(emul, MOTIONSENSE_TYPE_GYRO, 100.0f, 50.0f,
				   -25.0f);

	/* Motion sense loop processes the FIFO */
	k_msleep(100);

	count = motion_sense_fifo_read(sizeof(data), ARRAY_SIZE(data), data,
				       &read_byte_count);
	zassert_equal(2, count, "Expected 2 entries, got %d", count);
	zassert_equal(sizeof(data), read_byte_count, "FIFO read %u bytes",
		      read_byte_count);
	zassert_equal(MOTIONSENSE_SENSOR_FLAG_TIMESTAMP, data[0].flags);
	zassert_equal(GYR_SENSOR_ID, data[0].sensor_num);
	zassert_equal(0, data[1].flags);
	zassert_equal(GYR_SENSOR_ID, data[1].sensor_num);
	zassert_within((100 * INT16_MAX) / gyr->current_range, data[1].data[X],
		       1);
	zassert_within((50 * INT16_MAX) / gyr->current_range, data[1].data[Y],
		       1);
	zassert_within((-25 * INT16_MAX) / gyr->current_range, data[1].data[Z],
		       1);
}
