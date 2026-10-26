/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accelgyro_lsm6dso.h"
#include "driver/accelgyro_lsm6dso_public.h"
#include "emul/emul_lsm6dso.h"
#include "i2c.h"
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
	static struct ec_response_motion_sensor_data data[4];

	const struct gpio_dt_spec spec =
		GPIO_DT_SPEC_GET(LSM6DSO_NODE, int_gpios);
	int count;
	uint16_t read_byte_count;

	/* Set data rate to 13Hz, without this the FIFO will never be enabled */
	acc->config[SENSOR_CONFIG_AP].odr = 13000;
	acc->drv->set_data_rate(acc, 13000, 1);
	k_msleep(100);

	/* The lsm6dso throws away a few samples after an ODR change, burn
	 * through those so that the next sample would get on the FIFO if
	 * enabled.
	 */
	for (int i = 0; i < LSM6DSO_DISCARD_SAMPLES; ++i) {
		emul_lsm6dso_append_sample(emul, MOTIONSENSE_TYPE_ACCEL, 1.0f,
					   0.0f, 0.0f);
	}
	k_msleep(100);

	/* Disable the interrupts */
	acc->drv->enable_interrupt(acc, false);

	/* Clear out the soft motionsense FIFO */
	motion_sense_fifo_reset();

	/* Add an accel sample (1g, 0g, 0g) */
	emul_lsm6dso_append_sample(emul, MOTIONSENSE_TYPE_ACCEL, 1.0f, 0.0f,
				   0.0f);
	k_msleep(100);

	/* Interrupt should be deasserted and no events should be on the FIFO */
	count = motion_sense_fifo_read(sizeof(data), 4, data, &read_byte_count);
	zassert_equal(0, gpio_pin_get_dt(&spec));
	zassert_equal(0, count, "FIFO had %d entries", count);
	zassert_equal(0, read_byte_count, "FIFO read %u bytes",
		      read_byte_count);

	/* Flush the FIFO */
	uint32_t event = CONFIG_ACCEL_LSM6DSO_INT_EVENT;
	zassert_equal(EC_SUCCESS, acc->drv->irq_handler(acc, &event));

	/* Enable the interrupts */
	acc->drv->enable_interrupt(acc, true);

	/* Add another accel sample (1g, 0g, 0g) */
	emul_lsm6dso_append_sample(emul, MOTIONSENSE_TYPE_ACCEL, 1.0f, 0.0f,
				   0.0f);

	/* After we sleep, the motion sense loop should read the sample */
	k_msleep(100);
	count = motion_sense_fifo_read(sizeof(data), 4, data, &read_byte_count);
	zassert_equal(0, gpio_pin_get_dt(&spec));
	zassert_true(count >= 1, "FIFO had %d entries", count);
	zassert_equal(ACC_SENSOR_ID, data[count - 1].sensor_num,
		      "Sensor # expected %d, was %u", ACC_SENSOR_ID,
		      data[count - 1].sensor_num);
	zassert_equal(0, data[count - 1].flags);
	zassert_within(INT16_MAX / acc->current_range, data[count - 1].data[X],
		       1);
	zassert_within(0x0000, data[count - 1].data[Y], 1);
	zassert_within(0x0000, data[count - 1].data[Z], 1);
}

ZTEST(lsm6dso, test_gyro_sample)
{
	static struct ec_response_motion_sensor_data data[4];
	int count;
	uint16_t read_byte_count;

	/* Configure Gyro ODR and burn discard samples */
	gyr->config[SENSOR_CONFIG_AP].odr = 13000;
	gyr->drv->set_data_rate(gyr, 13000, 1);
	gyr->oversampling_ratio = 1;
	k_msleep(100);
	for (int i = 0; i < LSM6DSO_DISCARD_SAMPLES; ++i) {
		emul_lsm6dso_append_sample(emul, MOTIONSENSE_TYPE_GYRO, 100.0f,
					   0.0f, 0.0f);
	}
	k_msleep(100);
	motion_sense_fifo_reset();

	/* Enable interrupts */
	acc->drv->enable_interrupt(acc, true);

	/* Append gyro sample (100 dps) */
	emul_lsm6dso_append_sample(emul, MOTIONSENSE_TYPE_GYRO, 100.0f, 0.0f,
				   0.0f);

	/* Motion sense loop processes the FIFO */
	k_msleep(100);

	count = motion_sense_fifo_read(sizeof(data), 4, data, &read_byte_count);
	zassert_true(count >= 1, "Expected at least 1 entry, got %d", count);
	zassert_equal(GYR_SENSOR_ID, data[count - 1].sensor_num);
	zassert_equal(0, data[count - 1].flags);
}

ZTEST(lsm6dso, test_push_fifo_data_tag_bounds)
{
	static struct ec_response_motion_sensor_data data[4];
	const struct {
		uint8_t raw_tag;
		int sensor_num; /* -1 if the sample must be dropped */
	} cases[] = {
		{ LSM6DSO_GYRO_TAG - 1, -1 },
		{ LSM6DSO_GYRO_TAG, GYR_SENSOR_ID },
		{ LSM6DSO_ACC_TAG, ACC_SENSOR_ID },
		{ LSM6DSO_ACC_TAG + 1, -1 },
		{ 0xff >> LSM6DSO_FIFO_TAG_SHIFT, -1 },
	};
	uint32_t event = CONFIG_ACCEL_LSM6DSO_INT_EVENT;
	uint16_t read_byte_count;
	int count;

	/* Enable the FIFO for both sensors and handle it manually. */
	acc->drv->enable_interrupt(acc, false);
	zassert_ok(acc->drv->set_data_rate(acc, LSM6DSO_ODR_MIN_VAL, 0));
	zassert_ok(gyr->drv->set_data_rate(gyr, LSM6DSO_ODR_MIN_VAL, 0));
	acc->oversampling_ratio = 1;
	gyr->oversampling_ratio = 1;

	/* Burn the samples discarded after an ODR change. */
	for (int i = 0; i < LSM6DSO_DISCARD_SAMPLES; i++) {
		emul_lsm6dso_append_raw_tag(emul, LSM6DSO_GYRO_TAG, 0, 0, 0);
		emul_lsm6dso_append_raw_tag(emul, LSM6DSO_ACC_TAG, 0, 0, 0);
	}
	zassert_ok(acc->drv->irq_handler(acc, &event));

	for (int i = 0; i < ARRAY_SIZE(cases); i++) {
		motion_sense_fifo_reset();
		emul_lsm6dso_append_raw_tag(emul, cases[i].raw_tag, 0, 0, 0);
		zassert_ok(acc->drv->irq_handler(acc, &event));

		count = motion_sense_fifo_read(sizeof(data), ARRAY_SIZE(data),
					       data, &read_byte_count);
		if (cases[i].sensor_num < 0) {
			zassert_equal(count, 0, "tag 0x%02x", cases[i].raw_tag);
		} else {
			zassert_true(count >= 1, "tag 0x%02x",
				     cases[i].raw_tag);
			zassert_equal(data[count - 1].sensor_num,
				      cases[i].sensor_num, "tag 0x%02x",
				      cases[i].raw_tag);
		}
	}

	zassert_ok(acc->drv->set_data_rate(acc, 0, 0));
	zassert_ok(gyr->drv->set_data_rate(gyr, 0, 0));
	acc->drv->enable_interrupt(acc, true);
}

static int read_fifo_ctrl3(void)
{
	int ctrl3;

	zassert_ok(i2c_read8(acc->port, acc->i2c_spi_addr_flags,
			     LSM6DSO_FIFO_CTRL3_ADDR, &ctrl3));
	return ctrl3;
}

ZTEST(lsm6dso, test_accelgyro_config_fifo_power_off)
{
	zassert_ok(acc->drv->set_data_rate(acc, LSM6DSO_ODR_MIN_VAL, 0));
	zassert_ok(gyr->drv->set_data_rate(gyr, LSM6DSO_ODR_MIN_VAL, 0));
	zassert_not_equal(read_fifo_ctrl3() & LSM6DSO_FIFO_ODR_MASK(acc),
			  LSM6DSO_DIS_BIT);
	zassert_not_equal(read_fifo_ctrl3() & LSM6DSO_FIFO_ODR_MASK(gyr),
			  LSM6DSO_DIS_BIT);

	/* Powering off one sensor only disables its own FIFO batching. */
	zassert_ok(acc->drv->set_data_rate(acc, 0, 0));
	zassert_equal(read_fifo_ctrl3() & LSM6DSO_FIFO_ODR_MASK(acc),
		      LSM6DSO_DIS_BIT);
	zassert_not_equal(read_fifo_ctrl3() & LSM6DSO_FIFO_ODR_MASK(gyr),
			  LSM6DSO_DIS_BIT);

	zassert_ok(gyr->drv->set_data_rate(gyr, 0, 0));
	zassert_equal(read_fifo_ctrl3() & LSM6DSO_FIFO_ODR_MASK(gyr),
		      LSM6DSO_DIS_BIT);
}

ZTEST(lsm6dso, test_set_data_rate_fifo_error)
{
	struct i2c_common_emul_data *common =
		emul_lsm6dso_get_i2c_common_data(emul);
	const int fifo_regs[] = { LSM6DSO_FIFO_CTRL3_ADDR,
				  LSM6DSO_FIFO_CTRL4_ADDR };

	for (int i = 0; i < ARRAY_SIZE(fifo_regs); i++) {
		/* Start powered off so the new ODR rewrites FIFO_CTRL3. */
		zassert_ok(acc->drv->set_data_rate(acc, 0, 0));

		i2c_common_emul_set_write_fail_reg(common, fifo_regs[i]);
		zassert_not_equal(
			acc->drv->set_data_rate(acc, LSM6DSO_ODR_MIN_VAL, 0),
			EC_SUCCESS, "reg 0x%02x", fifo_regs[i]);
		i2c_common_emul_set_write_fail_reg(common,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
	}

	zassert_ok(acc->drv->set_data_rate(acc, 0, 0));
}

ZTEST(lsm6dso, test_set_range_write_error)
{
	struct i2c_common_emul_data *common =
		emul_lsm6dso_get_i2c_common_data(emul);
	const int orig_range = acc->current_range;

	zassert_ok(acc->drv->set_range(acc, LSM6DSO_ACCEL_FS_MAX_VAL / 2, 0));

	i2c_common_emul_set_write_fail_reg(common, LSM6DSO_ACCEL_FS_ADDR);
	zassert_not_equal(acc->drv->set_range(acc, LSM6DSO_ACCEL_FS_MAX_VAL, 0),
			  EC_SUCCESS);
	zassert_equal(acc->current_range, LSM6DSO_ACCEL_FS_MAX_VAL / 2);
	i2c_common_emul_set_write_fail_reg(common, I2C_COMMON_EMUL_NO_FAIL_REG);

	zassert_ok(acc->drv->set_range(acc, orig_range, 0));
}
