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

ZTEST_USER(lis2dh, test_lis2dh__samples_fifo_read)
{
	struct stprivate_data *drvdata = acc->drv_data;
	int16_t fake_sample[] = { 10, 20, 30, 40, 50, 60 };
	uint32_t evt = ACCEL_LIS2DH_INT_EVENT;
	static struct ec_response_motion_sensor_data
		fifo_data[ARRAY_SIZE(fake_sample) / 3 * 2];
	int count;
	uint16_t read_byte_count;
	int i;

	/* Basic initialization works. */
	zassert_ok(lis2dh_drv.init(acc));

	/* Clear out the soft motionsense FIFO */
	motion_sense_fifo_reset();

	/* Set data rate to 10Hz, without this the FIFO will never be enabled */
	acc->config[SENSOR_CONFIG_AP].odr = 10000;
	acc->drv->set_data_rate(acc, 10000, 1);
	acc->oversampling_ratio = 1;

	/* Verify fifo mode is enabled */
	zassert_true(lis2dh_emul_is_fifo_enabled(emul));

	/* Reading requires a range to be set. Use 1 so it has no effect
	 * when scaling samples. Also need to set the sensor resolution
	 * manually.
	 */
	lis2dh_drv.set_range(acc, 1, 0);
	drvdata->resol = LIS2DH_RESOLUTION;

	/*
	 * Samples from the lis2dh are left aligned so shift left by requiired
	 * amount.
	 */
	for (i = 0; i < ARRAY_SIZE(fake_sample); i++) {
		fake_sample[i] <<= (15 - LIS2DH_RESOLUTION);
	}
	/* Add data to the lis2dh emulated fifo */
	lis2dh_emul_set_fifo_data(emul, fake_sample, sizeof(fake_sample));

	/* Run interrupt handler */
	zassert_ok(lis2dh_drv.irq_handler(acc, &evt));

	/* Read data has been queue up in the motion_sense fifo */
	count = motion_sense_fifo_read(sizeof(fifo_data),
				       ARRAY_SIZE(fake_sample) / 3 * 2,
				       fifo_data, &read_byte_count);

	/*
	 * Each accel sample is paired with a timestamp entry so there should be
	 * 2 entries for each 3 samples in the fake_sample array.
	 */
	zassert_equal(count, ARRAY_SIZE(fake_sample) / 3 * 2);

	/*
	 * Validate each set of sensor XYZ samples.
	 * fifo_data[0] -> timestamp, fifo_data[1] -> sensor XYZ sample
	 * fifo_data[2] -> timestamp, fifo_data[3] -> sensor XYZ sample
	 */
	for (i = 0; i < ARRAY_SIZE(fake_sample) / 3; i++) {
		zassert_equal(fifo_data[i * 2].flags,
			      MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
		zassert_equal(fifo_data[i * 2].sensor_num, ACC_SENSOR_ID);
		zassert_equal(fifo_data[i * 2 + 1].data[X], fake_sample[i * 3]);
		zassert_equal(fifo_data[i * 2 + 1].data[Y],
			      fake_sample[i * 3 + 1]);
		zassert_equal(fifo_data[i * 2 + 1].data[Z],
			      fake_sample[i * 3 + 2]);
	}
}

ZTEST_USER(lis2dh, test_lis2dh__samples_fifo_read_long)
{
	struct stprivate_data *drvdata = acc->drv_data;
	int16_t fake_sample[(FIFO_READ_LEN / OUT_XYZ_SIZE) * 3 + 6];
	uint32_t evt = ACCEL_LIS2DH_INT_EVENT;
	static struct ec_response_motion_sensor_data
		fifo_data[ARRAY_SIZE(fake_sample) / 3 * 2];
	int count;
	uint16_t read_byte_count;
	int i;

	/* Basic initialization works. */
	zassert_ok(lis2dh_drv.init(acc));

	/* Clear out the soft motionsense FIFO */
	motion_sense_fifo_reset();

	/* Set data rate to 10Hz, without this the FIFO will never be enabled */
	acc->config[SENSOR_CONFIG_AP].odr = 10000;
	acc->drv->set_data_rate(acc, 10000, 1);
	acc->oversampling_ratio = 1;

	/* Verify fifo mode is enabled */
	zassert_true(lis2dh_emul_is_fifo_enabled(emul));

	/* Reading requires a range to be set. Use 1 so it has no effect
	 * when scaling samples. Also need to set the sensor resolution
	 * manually.
	 */
	lis2dh_drv.set_range(acc, 1, 0);
	drvdata->resol = LIS2DH_RESOLUTION;

	/*
	 * Create samples of 2, 4, 6, ... 8, 10, 12, ... 14, 16, 18 for 10 XYZ
	 * sample triplets. This will exceed FIFO_READ_LEN;
	 */
	for (i = 0; i < ARRAY_SIZE(fake_sample); i++) {
		fake_sample[i] = ((i + 1) * 2) << (15 - LIS2DH_RESOLUTION);
	}

	/* Add data to the lis2dh emulated fifo */
	lis2dh_emul_set_fifo_data(emul, fake_sample,
				  (uint8_t)(sizeof(fake_sample)));

	/* Run interrupt handler */
	zassert_ok(lis2dh_drv.irq_handler(acc, &evt));

	/* Read data has been queue up in the motion_sense fifo */
	count = motion_sense_fifo_read(sizeof(fifo_data),
				       ARRAY_SIZE(fake_sample) / 3 * 2,
				       fifo_data, &read_byte_count);

	/*
	 * Each accel sample is paired with a timestamp entry so there should be
	 * 2 entries for each 3 samples in the fake_sample array.
	 */
	zassert_equal(count, ARRAY_SIZE(fake_sample) / 3 * 2);

	/*
	 * Validate each set of sensor XYZ samples.
	 * fifo_data[0] -> timestamp, fifo_data[1] -> sensor XYZ sample
	 * fifo_data[2] -> timestamp, fifo_data[3] -> sensor XYZ sample
	 */
	for (i = 0; i < ARRAY_SIZE(fake_sample) / 3; i++) {
		zassert_equal(fifo_data[i * 2].flags,
			      MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
		zassert_equal(fifo_data[i * 2].sensor_num, ACC_SENSOR_ID);
		zassert_equal(fifo_data[i * 2 + 1].data[X], fake_sample[i * 3]);
		zassert_equal(fifo_data[i * 2 + 1].data[Y],
			      fake_sample[i * 3 + 1]);
		zassert_equal(fifo_data[i * 2 + 1].data[Z],
			      fake_sample[i * 3 + 2]);
	}
}

ZTEST_USER(lis2dh, test_lis2dh__samples_fifo_read_fail)
{
	struct stprivate_data *drvdata = acc->drv_data;
	int16_t fake_sample[] = { 10, 20, 30, 40, 50, 60 };
	uint32_t evt = ACCEL_LIS2DH_INT_EVENT;
	struct i2c_common_emul_data *common_data =
		emul_lis2dh_get_i2c_common_data(emul);
	int i;
	int rv;

	/* Basic initialization works. */
	zassert_ok(lis2dh_drv.init(acc));

	/* Clear out the soft motionsense FIFO */
	motion_sense_fifo_reset();

	/* Set data rate to 10Hz, without this the FIFO will never be enabled */
	acc->config[SENSOR_CONFIG_AP].odr = 10000;
	acc->drv->set_data_rate(acc, 10000, 1);
	acc->oversampling_ratio = 1;

	/* Verify fifo mode is enabled */
	zassert_true(lis2dh_emul_is_fifo_enabled(emul));

	/* Reading requires a range to be set. Use 1 so it has no effect
	 * when scaling samples. Also need to set the sensor resolution
	 * manually.
	 */
	lis2dh_drv.set_range(acc, 1, 0);
	drvdata->resol = LIS2DH_RESOLUTION;

	/*
	 * Samples from the lis2dh are left aligned so shift left by requiired
	 * amount.
	 */
	for (i = 0; i < ARRAY_SIZE(fake_sample); i++) {
		fake_sample[i] <<= (15 - LIS2DH_RESOLUTION);
	}
	/* Add data to the lis2dh emulated fifo */
	lis2dh_emul_set_fifo_data(emul, fake_sample, sizeof(fake_sample));

	/* Set incorrect interrupt event */
	evt = 0;
	/* Run interrupt handler */
	rv = lis2dh_drv.irq_handler(acc, &evt);
	zassert_equal(rv, EC_ERROR_NOT_HANDLED);

	/* Set FIFO_SRC_REG read to fail */
	i2c_common_emul_set_read_fail_reg(common_data, LIS2DH_FIFO_SRC_ADDR);
	evt = ACCEL_LIS2DH_INT_EVENT;
	rv = lis2dh_drv.irq_handler(acc, &evt);
	zassert_equal(EC_ERROR_INVAL, rv);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set LIS2DH_OUT_X_L_ADDR read to fail */
	i2c_common_emul_set_read_fail_reg(common_data,
					  0x80 | LIS2DH_OUT_X_L_ADDR);
	rv = lis2dh_drv.irq_handler(acc, &evt);
	zassert_equal(EC_ERROR_INVAL, rv);
}

ZTEST_USER(lis2dh, test_lis2dh__samples_fifo_read_spoof)
{
	struct stprivate_data *drvdata = acc->drv_data;
	int16_t fake_sample[] = { 10, 20, 30 };
	uint32_t evt = ACCEL_LIS2DH_INT_EVENT;
	static struct ec_response_motion_sensor_data
		fifo_data[ARRAY_SIZE(fake_sample) / 3 * 2];
	int count;
	uint16_t read_byte_count;
	int i;

	/* Basic initialization works. */
	zassert_ok(lis2dh_drv.init(acc));

	/* Clear out the soft motionsense FIFO */
	motion_sense_fifo_reset();

	/* Set data rate to 10Hz, without this the FIFO will never be enabled */
	acc->config[SENSOR_CONFIG_AP].odr = 10000;
	acc->drv->set_data_rate(acc, 10000, 1);
	acc->oversampling_ratio = 1;

	/* Verify fifo mode is enabled */
	zassert_true(lis2dh_emul_is_fifo_enabled(emul));

	/* Reading requires a range to be set. Use 1 so it has no effect
	 * when scaling samples. Also need to set the sensor resolution
	 * manually.
	 */
	lis2dh_drv.set_range(acc, 1, 0);
	drvdata->resol = LIS2DH_RESOLUTION;

	/* Enable spoof fifo spoof mode */
	acc->flags |= MOTIONSENSE_FLAG_IN_SPOOF_MODE;

	/* Set values to be spoofed */
	acc->spoof_xyz[X] = 50 << (15 - LIS2DH_RESOLUTION);
	acc->spoof_xyz[Y] = 60 << (15 - LIS2DH_RESOLUTION);
	acc->spoof_xyz[Z] = 70 << (15 - LIS2DH_RESOLUTION);

	/*
	 * Samples from the lis2dh are left aligned so shift left by requiired
	 * amount.
	 */
	for (i = 0; i < ARRAY_SIZE(fake_sample); i++) {
		fake_sample[i] <<= (15 - LIS2DH_RESOLUTION);
	}
	/* Add data to the lis2dh emulated fifo */
	lis2dh_emul_set_fifo_data(emul, fake_sample, sizeof(fake_sample));

	/* Run interrupt handler */
	zassert_ok(lis2dh_drv.irq_handler(acc, &evt));

	/* Clear fifo spoof mode flag */
	acc->flags &= ~MOTIONSENSE_FLAG_IN_SPOOF_MODE;

	/* Read data has been queue up in the motion_sense fifo */
	count = motion_sense_fifo_read(sizeof(fifo_data),
				       ARRAY_SIZE(fake_sample) / 3 * 2,
				       fifo_data, &read_byte_count);

	/*
	 * Each accel sample is paired with a timestamp entry so there should be
	 * 2 entries for each 3 samples in the fake_sample array.
	 */
	zassert_equal(count, ARRAY_SIZE(fake_sample) / 3 * 2);

	/* Data from fifo should match spoof values */
	zassert_equal(fifo_data[1].data[X], acc->spoof_xyz[X]);
	zassert_equal(fifo_data[1].data[Y], acc->spoof_xyz[Y]);
	zassert_equal(fifo_data[1].data[Z], acc->spoof_xyz[Z]);
}

ZTEST_USER(lis2dh, test_lis2dh__trigger_interrupt)
{
	struct stprivate_data *drvdata = acc->drv_data;
	int16_t fake_sample[] = { 10, 20, 30, 40, 50, 60 };
	int i;

	/* Basic initialization works. */
	zassert_ok(lis2dh_drv.init(acc));

	/* Clear out the soft motionsense FIFO */
	motion_sense_fifo_reset();

	/* Set data rate to 10Hz, without this the FIFO will never be enabled */
	acc->config[SENSOR_CONFIG_AP].odr = 10000;
	acc->drv->set_data_rate(acc, 10000, 1);
	acc->oversampling_ratio = 1;

	/* Verify fifo mode is enabled */
	zassert_true(lis2dh_emul_is_fifo_enabled(emul));

	/* Reading requires a range to be set. Use 1 so it has no effect
	 * when scaling samples. Also need to set the sensor resolution
	 * manually.
	 */
	lis2dh_drv.set_range(acc, 1, 0);
	drvdata->resol = LIS2DH_RESOLUTION;

	/* Interrupt should be inactive */
	zassert_equal(lis2dh_emul_get_interrupt_pin(emul), 0);

	/*
	 * Samples from the lis2dh are left aligned so shift left by requiired
	 * amount.
	 */
	for (i = 0; i < ARRAY_SIZE(fake_sample); i++) {
		fake_sample[i] <<= (15 - LIS2DH_RESOLUTION);
	}
	/* Add data to the lis2dh emulated fifo */
	lis2dh_emul_set_fifo_data(emul, fake_sample, sizeof(fake_sample));

	/* lis2dh fifo data should exceed watermark */
	zassert_equal(lis2dh_emul_get_interrupt_pin(emul), 1);
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

ZTEST(lis2dh, test_lis2dh_init__fail_interrupt_config)
{
	struct i2c_common_emul_data *common_data =
		emul_lis2dh_get_i2c_common_data(emul);
	int rv;

	i2c_common_emul_set_write_fail_reg(common_data, LIS2DH_FIFO_CTRL_ADDR);
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

	/* Configure LIS2DH_FIFO_CTRL_ADDR write to fail */
	i2c_common_emul_set_write_fail_reg(common_data, LIS2DH_FIFO_CTRL_ADDR);
	rv = lis2dh_drv.set_range(acc, 2, 0);
	zassert_equal(rv, EC_ERROR_INVAL);
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

ZTEST_SUITE(lis2dh, drivers_predicate_post_main, NULL, lis2dh_before, NULL,
	    NULL);
