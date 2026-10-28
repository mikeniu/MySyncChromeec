/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/als_cm32183.h"
#include "driver/als_cm36781.h"
#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

extern enum chipset_state_mask sensor_active;

ZTEST_SUITE(motion_sense, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST_USER(motion_sense, test_ec_motion_sensor_fill_values)
{
	struct ec_response_motion_sensor_data dst = {
		.data = { 1, 2, 3 },
	};
	const int32_t v[] = { 4, 5, 6 };

	ec_motion_sensor_fill_values(&dst, v);
	zassert_equal(dst.data[0], v[0]);
	zassert_equal(dst.data[1], v[1]);
	zassert_equal(dst.data[2], v[2]);
}

ZTEST_USER(motion_sense, test_ec_motion_sensor_clamp_i16)
{
	zassert_equal(ec_motion_sensor_clamp_i16(0), 0);
	zassert_equal(ec_motion_sensor_clamp_i16(200), 200);
	zassert_equal(ec_motion_sensor_clamp_i16(-512), -512);
	zassert_equal(ec_motion_sensor_clamp_i16(INT16_MAX + 1), INT16_MAX,
		      NULL);
	zassert_equal(ec_motion_sensor_clamp_i16(INT16_MIN - 1), INT16_MIN,
		      NULL);
}

ZTEST_USER(motion_sense, test_ec_motion_sense_get_ec_config)
{
	/* illegal state, should be translated to S5 */
	sensor_active = 42;
	zassert_equal(motion_sense_get_ec_config(), SENSOR_CONFIG_EC_S5);
	/* all valid states */
	sensor_active = SENSOR_ACTIVE_S0;
	zassert_equal(motion_sense_get_ec_config(), SENSOR_CONFIG_EC_S0);
	sensor_active = SENSOR_ACTIVE_S3;
	zassert_equal(motion_sense_get_ec_config(), SENSOR_CONFIG_EC_S3);
	sensor_active = SENSOR_ACTIVE_S5;
	zassert_equal(motion_sense_get_ec_config(), SENSOR_CONFIG_EC_S5);
}

void fifo_stage_unit(struct ec_response_motion_sensor_data *data,
		     struct motion_sensor_t *sensor, int valid_data);
/*
 * Validate that the fifo parsing logic can skip invalid entries.
 * See b/290725559 for details.
 */
ZTEST_USER(motion_sense, test_fifo_data_validation)
{
	struct ec_response_motion_sensor_data data;

	/* Insert just one data entry, no timestamp. */
	data.flags = 0;
	data.sensor_num = 0;

	fifo_stage_unit(&data, NULL, 0);
	motion_sense_fifo_commit_data();

	/*
	 * This test fails if the function above crashes.
	 * Nothing to do here.
	 */
}

ZTEST_USER(motion_sense, test_als_cm32183_cm36781_get_offset)
{
	struct cm32183_drv_data cm32183_data = {
		.offset = 42,
	};
	struct motion_sensor_t cm32183_sensor = {
		.drv = &cm32183_drv,
		.drv_data = &cm32183_data,
	};
	struct cm36781_drv_data cm36781_data = {
		.offset = -17,
	};
	struct motion_sensor_t cm36781_sensor = {
		.drv = &cm36781_drv,
		.drv_data = &cm36781_data,
	};
	int16_t offset[3] = { 0 };
	int16_t temp = 0;

	zassert_equal(EC_SUCCESS, cm32183_sensor.drv->get_offset(
					  &cm32183_sensor, offset, &temp));
	zassert_equal(42, offset[0]);
	zassert_equal((int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP, temp);

	offset[0] = 0;
	temp = 0;
	zassert_equal(EC_SUCCESS, cm36781_sensor.drv->get_offset(
					  &cm36781_sensor, offset, &temp));
	zassert_equal(-17, offset[0]);
	zassert_equal((int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP, temp);
}
