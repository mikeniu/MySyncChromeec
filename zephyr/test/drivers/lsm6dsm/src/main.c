/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/accelgyro_lsm6dsm_public.h"
#include "emul/emul_lsm6dsm.h"
#include "i2c.h"
#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>

#define LSM6DSM_NODE DT_NODELABEL(lsm6dsm_emul)
#define ACC_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_lsm6dsm_accel))
#define GYR_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_lsm6dsm_gyro))

static const struct emul *emul = EMUL_DT_GET(LSM6DSM_NODE);
static struct motion_sensor_t *acc = &motion_sensors[ACC_SENSOR_ID];
// static struct motion_sensor_t *gyr = &motion_sensors[GYR_SENSOR_ID];

ZTEST_SUITE(lsm6dsm, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST(lsm6dsm, test_read_temp)
{
	int temp = 0;

	/* Negative temperature offset (-1 C from 25 C = 24 C = 297 K) */
	zassert_ok(i2c_write8(acc->port, acc->i2c_spi_addr_flags,
			      LSM6DSM_OUT_TEMP_L_ADDR + 1, 0xff));
	zassert_ok(acc->drv->read_temp(acc, &temp));
	zassert_equal(temp, C_TO_K(24));
	zassert_ok(i2c_write8(acc->port, acc->i2c_spi_addr_flags,
			      LSM6DSM_OUT_TEMP_L_ADDR + 1, 0x00));
}

ZTEST(lsm6dsm, test_disable_interrupt)
{
	static struct ec_response_motion_sensor_data data[2];

	const struct gpio_dt_spec spec =
		GPIO_DT_SPEC_GET(LSM6DSM_NODE, int_gpios);
	int count;
	uint16_t read_byte_count;

	/* Set data rate to 10Hz, without this the FIFO will never be enabled */
	acc->config[SENSOR_CONFIG_AP].odr = 13000;
	acc->drv->set_data_rate(acc, 13000, 1);
	/* Set the oversampling ratio to 1 so we don't drop any samples. */
	// acc->oversampling_ratio = 1;
	k_msleep(100);

	/* The lsm6dsm throws away a few samples after an ODR change, burn
	 * through those so that the next sample would get on the FIFO if
	 * enabled.
	 */
	for (int i = 0; i < LSM6DSM_DISCARD_SAMPLES; ++i) {
		emul_lsm6dsm_append_sample(emul, MOTIONSENSE_TYPE_ACCEL, 1.0f,
					   0.0f, 0.0f);
	}

	/* Disable the interrupts */
	acc->drv->enable_interrupt(acc, false);

	/* Clear out the soft motionsense FIFO */
	motion_sense_fifo_reset();

	/* Add an accel sample (1g, 0g, 0g) */
	emul_lsm6dsm_append_sample(emul, MOTIONSENSE_TYPE_ACCEL, 1.0f, 0.0f,
				   0.0f);
	k_msleep(100);

	/* Interrupt should be deasserted and no events should be on the FIFO */
	count = motion_sense_fifo_read(sizeof(data), 2, data, &read_byte_count);
	zassert_equal(0, gpio_pin_get_dt(&spec));
	zassert_equal(0, count, "FIFO had %d entries", count);
	zassert_equal(0, read_byte_count, "FIFO read %u bytes",
		      read_byte_count);

	/* Flush the FIFO */
	uint32_t event = CONFIG_ACCEL_LSM6DSM_INT_EVENT;
	zassert_equal(EC_SUCCESS, acc->drv->irq_handler(acc, &event));

	/* Enable the interrupts */
	acc->drv->enable_interrupt(acc, true);

	/* Add another accel sample (1g, 0g, 0g) */
	emul_lsm6dsm_append_sample(emul, MOTIONSENSE_TYPE_ACCEL, 1.0f, 0.0f,
				   0.0f);

	/* Interrupt should be asserted */
	zassert_equal(1, gpio_pin_get_dt(&spec));

	/* After we sleep, the motion sense loop should read the sample */
	k_msleep(100);
	count = motion_sense_fifo_read(sizeof(data), 2, data, &read_byte_count);
	zassert_equal(0, gpio_pin_get_dt(&spec));
	zassert_equal(2, count, "FIFO had %d entries", count);
	zassert_equal(sizeof(data), read_byte_count, "FIFO read %u bytes",
		      read_byte_count);
	/* First sample should be a timestamp */
	zassert_equal(MOTIONSENSE_SENSOR_FLAG_TIMESTAMP, data[0].flags,
		      "Flags was %u", data[0].flags);
	zassert_equal(ACC_SENSOR_ID, data[0].sensor_num,
		      "Sensor # expected %d, was %u", ACC_SENSOR_ID,
		      data[0].sensor_num);

	/* Second sample is the data */
	zassert_equal(0, data[1].flags, "Flags was %u", data[1].flags);
	zassert_equal(ACC_SENSOR_ID, data[1].sensor_num,
		      "Sensor # expected %d, was %u", ACC_SENSOR_ID,
		      data[1].sensor_num);
	zassert_within(INT16_MAX / acc->current_range, data[1].data[X], 1);
	zassert_within(0x0000, data[1].data[Y], 1);
	zassert_within(0x0000, data[1].data[Z], 1);
}
