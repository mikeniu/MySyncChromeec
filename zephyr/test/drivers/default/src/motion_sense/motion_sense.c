/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "chipset.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

extern enum chipset_state_mask sensor_active;
int sensor_init_done(struct motion_sensor_t *s);
int motion_sense_set_data_rate(struct motion_sensor_t *sensor);
bool motion_sensor_in_forced_mode(const struct motion_sensor_t *sensor);
int sensor_board_is_lid_angle_available(void);
void fifo_stage_unit(struct ec_response_motion_sensor_data *data,
		     struct motion_sensor_t *sensor, int valid_data);
static void motion_sense_test_before(void *state)
{
	ARG_UNUSED(state);
	sensor_active = SENSOR_ACTIVE_S0;
	for (int i = 0; i < motion_sensor_count; i++) {
		motion_sensors[i].state = SENSOR_READY;
		motion_sensors[i].flags &= ~MOTIONSENSE_FLAG_IN_SPOOF_MODE;
		motion_sensors[i].config[SENSOR_CONFIG_AP].odr = 100000;
	}
	motion_sense_fifo_reset();
	motion_sense_fifo_reset_needed_flags();
}

ZTEST_SUITE(motion_sense, drivers_predicate_post_main, NULL,
	    motion_sense_test_before, NULL, NULL);

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
}

ZTEST(motion_sense, test_fifo_insert_async_event)
{
	struct ec_response_motion_sensor_data data[CONFIG_ACCEL_FIFO_SIZE];
	uint16_t data_bytes_read = 0;
	int read_count;

	motion_sense_fifo_insert_async_event(&motion_sensors[0],
					     ASYNC_EVENT_FLUSH);
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	zassert_equal(read_count, 1);
	zassert_equal(data[0].flags, ASYNC_EVENT_FLUSH);
	zassert_equal(data[0].sensor_num, 0);
}

ZTEST(motion_sense, test_fifo_wake_up_needed)
{
	struct ec_response_motion_sensor_data data = {
		.flags = MOTIONSENSE_SENSOR_FLAG_WAKEUP,
		.sensor_num = 0,
	};

	motion_sense_fifo_stage_data(&data, &motion_sensors[0], 1, 0);
	motion_sense_fifo_commit_data();
	zassert_equal(motion_sense_fifo_wake_up_needed(), 1);

	motion_sense_fifo_reset_needed_flags();
	zassert_equal(motion_sense_fifo_wake_up_needed(), 0);
}

ZTEST(motion_sense, test_fifo_wake_up_needed_overflow)
{
	struct ec_response_motion_sensor_data data = {
		.flags = MOTIONSENSE_SENSOR_FLAG_WAKEUP,
		.sensor_num = 0,
	};

	motion_sense_fifo_stage_data(&data, &motion_sensors[0], 1, 0);

	/* Stage enough samples to overflow the FIFO and evict the wakeup item
	 */
	for (int i = 0; i < CONFIG_ACCEL_FIFO_SIZE; i++) {
		data.flags = 0;
		data.sensor_num = 0;
		motion_sense_fifo_stage_data(&data, &motion_sensors[0], 1, 0);
	}
	motion_sense_fifo_commit_data();

	/* Wake up flag should be remembered even though popped */
	zassert_equal(motion_sense_fifo_wake_up_needed(), 1);
}

ZTEST(motion_sense, test_fifo_adding_timestamp)
{
	struct ec_response_motion_sensor_data data[4];
	uint16_t data_bytes_read;
	int read_count;

	motion_sense_fifo_add_timestamp(12345);
	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 1);
	zassert_true(data[0].flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	zassert_equal(data[0].timestamp, 12345);
	zassert_equal(data[0].sensor_num, 0xff);
}

ZTEST(motion_sense, test_fifo_stage_data_sets_xyz)
{
	struct ec_response_motion_sensor_data data = {
		.flags = 0,
		.sensor_num = 0,
		.data = { 10, 20, 30 },
	};

	motion_sensors[0].oversampling_ratio = 1;
	motion_sense_fifo_stage_data(&data, &motion_sensors[0], 3, 0);
	zassert_equal(motion_sensors[0].xyz[0], 10);
	zassert_equal(motion_sensors[0].xyz[1], 20);
	zassert_equal(motion_sensors[0].xyz[2], 30);
}

ZTEST(motion_sense, test_fifo_stage_data_oversampling)
{
	struct ec_response_motion_sensor_data data[4] = { 0 };
	uint16_t data_bytes_read;
	int read_count;

	/* Ratio 2: first sample staged (timestamp + data = 2) */
	motion_sensors[0].oversampling_ratio = 2;
	motion_sensors[0].oversampling = 0;
	data[0].flags = 0;
	data[0].sensor_num = 0;
	data[0].data[0] = 1;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 0);
	motion_sense_fifo_commit_data();
	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 2); /* Timestamp + Data */

	/* Second sample data dropped (only timestamp staged = 1) */
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 0);
	motion_sense_fifo_commit_data();
	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 1);
	zassert_true(data[0].flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);

	/* Ratio 0: data dropped (only timestamp staged = 1) */
	motion_sensors[0].oversampling_ratio = 0;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 0);
	motion_sense_fifo_commit_data();
	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 1);
	zassert_true(data[0].flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
}

ZTEST(motion_sense, test_fifo_stage_data_evicts_with_timestamp)
{
	struct ec_response_motion_sensor_data data[CONFIG_ACCEL_FIFO_SIZE] = {
		0
	};
	uint16_t data_bytes_read;
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[1].oversampling_ratio = 1;
	motion_sensors[0].spreading_threshold = 0;
	motion_sensors[1].spreading_threshold = 0;

	/* Fill the fifo to capacity with timestamp+data pairs */
	for (int i = 0; i < CONFIG_ACCEL_FIFO_SIZE / 2; i++) {
		data[0].flags = 0;
		data[0].sensor_num = 0;
		data[0].data[0] = i;
		motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1,
					     10 * i);
	}

	/* Add 1 more element for sensor 1 which evicts the first element */
	data[0].flags = 0;
	data[0].sensor_num = 1;
	data[0].data[0] = 0x55;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[1], 1, 1000);
	motion_sense_fifo_commit_data();

	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	zassert_equal(read_count, CONFIG_ACCEL_FIFO_SIZE);
	zassert_true(data[0].flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	zassert_equal(data[0].sensor_num, 0);
	zassert_equal(data[0].timestamp, 10);
	zassert_equal(data[1].sensor_num, 0);
	zassert_equal(data[1].data[0], 1);
}

ZTEST(motion_sense, test_fifo_spreading)
{
	struct ec_response_motion_sensor_data data[4];
	uint16_t data_bytes_read;
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[1].oversampling_ratio = 1;
	motion_sensors[0].spreading_threshold = 0;
	motion_sensors[1].spreading_threshold = 0;
	motion_sense_set_data_period(0, 20);
	motion_sense_set_data_period(1, 20);

	/* 1. Different sensors: no spreading */
	data[0].flags = 0;
	data[0].sensor_num = 0;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 100);
	data[0].sensor_num = 1;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[1], 1, 100);
	motion_sense_fifo_commit_data();

	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 4);
	zassert_equal(data[0].timestamp, 100);
	zassert_equal(data[2].timestamp, 100);

	/* 2. Same sensor, different timestamps: no spreading */
	data[0].flags = 0;
	data[0].sensor_num = 0;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 200);
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 220);
	motion_sense_fifo_commit_data();

	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 4);
	zassert_equal(data[0].timestamp, 200);
	zassert_equal(data[2].timestamp, 220);

	/* 3. Same sensor, identical timestamps: timestamps spread */
	data[0].flags = 0;
	data[0].sensor_num = 0;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 300);
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 300);
	motion_sense_fifo_commit_data();

	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 4);
	zassert_equal(data[0].timestamp, 300);
	zassert_equal(data[2].timestamp, 320);
}

ZTEST(motion_sense, test_fifo_non_data_entries_and_info)
{
	struct ec_response_motion_sensor_data data[4];
	uint8_t fifo_info_buffer
		[sizeof(struct ec_response_motion_sense_fifo_info) +
		 sizeof(uint16_t) * MAX_MOTION_SENSORS];
	struct ec_response_motion_sense_fifo_info *fifo_info =
		(void *)fifo_info_buffer;
	uint16_t data_bytes_read;
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sense_set_data_period(0, 20);

	/* Insert non-data (ODR) entry followed by data */
	data[0].flags = MOTIONSENSE_SENSOR_FLAG_ODR;
	data[0].sensor_num = 0;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 100);

	data[0].flags = 0;
	data[0].sensor_num = 0;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 100);
	motion_sense_fifo_commit_data();

	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 4);
	zassert_true(data[0].flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	zassert_true(data[1].flags & MOTIONSENSE_SENSOR_FLAG_ODR);

	/* Check fifo info */
	motion_sense_fifo_get_info(fifo_info, false);
	zassert_equal(fifo_info->size, CONFIG_ACCEL_FIFO_SIZE);

	motion_sense_fifo_get_info(fifo_info, true);
	zassert_equal(fifo_info->total_lost, 0);

	/* Check bypass needed flag */
	data[0].flags = MOTIONSENSE_SENSOR_FLAG_BYPASS_FIFO;
	data[0].sensor_num = 0;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, 0);
	motion_sense_fifo_commit_data();
	zassert_equal(motion_sense_fifo_bypass_needed(), 1);
	motion_sense_fifo_reset_needed_flags();
	zassert_equal(motion_sense_fifo_bypass_needed(), 0);

	/* Check over threshold */
	zassert_equal(motion_sense_fifo_over_thres(), 0);
}

ZTEST(motion_sense, test_fifo_ap_interval)
{
	struct ec_response_motion_sensor_data data = {
		.flags = 0,
		.sensor_num = 0,
	};
	uint32_t now = __hw_clock_source_read();

	motion_sensors[0].config[SENSOR_CONFIG_AP].odr = 200000;
	motion_sensors[0].config[SENSOR_CONFIG_AP].ec_rate = 5000;
	motion_sensors[0].oversampling_ratio = 1;
	motion_sense_set_data_period(0, 5000);

	motion_sense_fifo_stage_data(&data, &motion_sensors[0], 1, now + 10000);
	motion_sense_fifo_commit_data();
	zassert_equal(motion_sense_fifo_interrupt_needed(), 1);
	motion_sense_fifo_reset_needed_flags();
	zassert_equal(motion_sense_fifo_interrupt_needed(), 0);
}

ZTEST(motion_sense, test_fifo_ap_interval_multiple_samples)
{
	struct ec_response_motion_sensor_data data[4] = { 0 };
	uint16_t data_bytes_read;
	int read_count;
	uint32_t now = __hw_clock_source_read();

	/* AP needs data every 2 samples (ec_rate = 10000, odr = 200000,
	 * data_period = 5000) */
	motion_sensors[0].config[SENSOR_CONFIG_AP].odr = 200000;
	motion_sensors[0].config[SENSOR_CONFIG_AP].ec_rate = 10000;
	motion_sensors[0].oversampling_ratio = 1;
	motion_sense_set_data_period(0, 5000);

	data[0].sensor_num = 0;
	data[0].flags = 0;

	/* Sample 1 */
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, now);
	motion_sense_fifo_commit_data();
	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 2);
	zassert_equal(motion_sense_fifo_interrupt_needed(), 0);

	/* Sample 2: 10ms later */
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1,
				     now + 10000);
	motion_sense_fifo_commit_data();
	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 2);
	zassert_equal(motion_sense_fifo_interrupt_needed(), 1);
	motion_sense_fifo_reset_needed_flags();
}

ZTEST(motion_sense, test_fifo_spread_data_on_overflow)
{
	const uint32_t now = __hw_clock_source_read();
	const int fill_count = (CONFIG_ACCEL_FIFO_SIZE / 2) - 1;
	struct ec_response_motion_sensor_data data[CONFIG_ACCEL_FIFO_SIZE] = {
		0
	};
	uint16_t data_bytes_read;
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[1].oversampling_ratio = 1;
	motion_sensors[0].spreading_threshold = 0;
	motion_sensors[1].spreading_threshold = 0;
	motion_sense_set_data_period(0, 20);

	/* Add 1 sample for sensor 1 (to be evicted) */
	data[0].flags = 0;
	data[0].sensor_num = 1;
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[1], 1, 0);

	/* Fill rest of fifo with paired timestamps */
	data[0].sensor_num = 0;
	for (int i = 0; i < fill_count; i++) {
		int ts = now - ((fill_count - i) / 2) * 10;
		motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1,
					     ts);
	}

	/* Insert async flush event */
	motion_sense_fifo_insert_async_event(&motion_sensors[0],
					     ASYNC_EVENT_FLUSH);

	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 4);
	zassert_true(data[0].flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	zassert_true(data[2].flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	zassert_equal(data[0].sensor_num, 0);
	zassert_not_equal(data[0].timestamp, data[2].timestamp);
}

ZTEST(motion_sense, test_fifo_spread_by_collection_rate)
{
	const uint32_t now = __hw_clock_source_read();
	struct ec_response_motion_sensor_data data[4];
	uint16_t data_bytes_read;
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[0].spreading_threshold = 0;
	motion_sense_set_data_period(0, 20);
	data[0].flags = 0;
	data[0].sensor_num = 0;

	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, now - 25);
	motion_sense_fifo_stage_data(&data[0], &motion_sensors[0], 1, now - 25);
	motion_sense_fifo_commit_data();

	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 4);
	zassert_true(data[0].flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	zassert_equal(data[0].timestamp, now - 25);
	zassert_true(data[2].flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	zassert_equal(data[2].timestamp, now - 5);
}

ZTEST(motion_sense, test_motion_sense_forced_mode_and_rates)
{
	uint32_t orig_flags = motion_sensors[0].flags;

	sensor_active = SENSOR_ACTIVE_S0;
	motion_sensors[0].flags |= MOTIONSENSE_FLAG_IN_FORCED_MODE;
	zassert_true(motion_sensor_in_forced_mode(&motion_sensors[0]));
	motion_sensors[0].flags &= ~MOTIONSENSE_FLAG_IN_FORCED_MODE;
	if (!(CONFIG_ACCEL_FORCE_MODE_MASK & BIT(0))) {
		zassert_false(motion_sensor_in_forced_mode(&motion_sensors[0]));
	}
	motion_sensors[0].flags = orig_flags;

	/* Test set data rate */
	sensor_active = SENSOR_ACTIVE_S0;
	motion_sensors[0].config[SENSOR_CONFIG_AP].odr = 100000;
	motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr = 50000;
	zassert_ok(motion_sense_set_data_rate(&motion_sensors[0]));
	zassert_true(motion_sensors[0].collection_rate > 0);
	zassert_equal(motion_sensors[0].oversampling_ratio, 1);

	motion_sensors[0].config[SENSOR_CONFIG_AP].odr = 0;
	motion_sensors[0].config[SENSOR_CONFIG_EC_S0].odr = 0;
	zassert_ok(motion_sense_set_data_rate(&motion_sensors[0]));
	zassert_equal(motion_sensors[0].oversampling_ratio, 0);
}

ZTEST(motion_sense, test_motion_sense_power_and_init)
{
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_equal(sensor_active, SENSOR_ACTIVE_S5);
	zassert_equal(motion_sensors[0].config[SENSOR_CONFIG_AP].odr, 0);
	zassert_equal(motion_sensors[0].config[SENSOR_CONFIG_AP].ec_rate, 0);

	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_equal(sensor_active, SENSOR_ACTIVE_S5);

	hook_notify(HOOK_CHIPSET_RESUME);
	zassert_equal(sensor_active, SENSOR_ACTIVE_S0);

	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_equal(sensor_active, SENSOR_ACTIVE_S3);

	motion_sensors[0].current_range = 2;
	zassert_ok(sensor_init_done(&motion_sensors[0]));
	zassert_equal(sensor_board_is_lid_angle_available(), 1);
}

ZTEST(motion_sense, test_motion_sense_push_raw_and_spoof)
{
	struct ec_response_motion_sensor_data data[4];
	uint16_t data_bytes_read;
	int read_count;

	motion_sensors[0].raw_xyz[X] = 100;
	motion_sensors[0].raw_xyz[Y] = 200;
	motion_sensors[0].raw_xyz[Z] = 300;
	motion_sensors[0].oversampling_ratio = 1;
	motion_sense_set_data_period(0, 20);

	/* 1. Normal push without spoof mode */
	motion_sense_push_raw_xyz(&motion_sensors[0]);
	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 2); /* Timestamp + Data */
	zassert_equal(data[1].data[0], 100);
	zassert_equal(data[1].data[1], 200);
	zassert_equal(data[1].data[2], 300);

	/* 2. Push with spoof mode enabled */
	motion_sensors[0].flags |= MOTIONSENSE_FLAG_IN_SPOOF_MODE;
	motion_sensors[0].spoof_xyz[X] = 400;
	motion_sensors[0].spoof_xyz[Y] = 500;
	motion_sensors[0].spoof_xyz[Z] = 600;

	motion_sense_push_raw_xyz(&motion_sensors[0]);
	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);
	zassert_equal(read_count, 2);
	zassert_equal(data[1].data[0], 400);
	zassert_equal(data[1].data[1], 500);
	zassert_equal(data[1].data[2], 600);

	motion_sensors[0].flags &= ~MOTIONSENSE_FLAG_IN_SPOOF_MODE;
}

ZTEST(motion_sense, test_motion_sense_host_commands)
{
	struct ec_params_motion_sense params;
	uint8_t response_buffer[256];
	struct ec_response_motion_sense *response =
		(struct ec_response_motion_sense *)response_buffer;
	struct host_cmd_handler_args args = {
		.command = EC_CMD_MOTION_SENSE_CMD,
		.version = 3,
		.params = &params,
		.params_size = sizeof(params),
		.response = response,
		.response_max = sizeof(response_buffer),
	};

	/* 1. MOTIONSENSE_CMD_DUMP */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_DUMP;
	params.dump.max_sensor_count = 2;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* 2. MOTIONSENSE_CMD_DATA */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_DATA;
	params.sensor_odr.sensor_num = 0;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* Invalid sensor number */
	params.sensor_odr.sensor_num = 0xff;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);

	/* 3. MOTIONSENSE_CMD_INFO */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_INFO;
	params.sensor_odr.sensor_num = 0;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* 4. MOTIONSENSE_CMD_EC_RATE */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_EC_RATE;
	params.ec_rate.sensor_num = 0;
	params.ec_rate.data = 100;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* 5. MOTIONSENSE_CMD_SENSOR_ODR */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_SENSOR_ODR;
	params.sensor_odr.sensor_num = 0;
	params.sensor_odr.data = 10000;
	params.sensor_odr.roundup = 1;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* 6. MOTIONSENSE_CMD_SENSOR_RANGE */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_SENSOR_RANGE;
	params.sensor_range.sensor_num = 0;
	params.sensor_range.data = 2;
	params.sensor_range.roundup = 1;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* 7. MOTIONSENSE_CMD_FIFO_INT_ENABLE */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_FIFO_INT_ENABLE;
	params.fifo_int_enable.enable = 1;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	params.fifo_int_enable.enable = 0;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	params.fifo_int_enable.enable = EC_MOTION_SENSE_NO_VALUE;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	params.fifo_int_enable.enable = 2;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);

	/* 8. MOTIONSENSE_CMD_FIFO_FLUSH */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_FIFO_FLUSH;
	params.sensor_odr.sensor_num = 0;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* 9. MOTIONSENSE_CMD_FIFO_INFO */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_FIFO_INFO;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* 10. MOTIONSENSE_CMD_FIFO_READ */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_FIFO_READ;
	params.fifo_read.max_data_vector = 4;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* 11. MOTIONSENSE_CMD_SPOOF */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_SPOOF;
	params.spoof.sensor_id = 0;
	params.spoof.spoof_enable = MOTIONSENSE_SPOOF_MODE_CUSTOM;
	params.spoof.components[0] = 100;
	params.spoof.components[1] = 200;
	params.spoof.components[2] = 300;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	params.spoof.spoof_enable = MOTIONSENSE_SPOOF_MODE_QUERY;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	zassert_equal(response->spoof.ret, 1);

	params.spoof.spoof_enable = MOTIONSENSE_SPOOF_MODE_LOCK_CURRENT;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	params.spoof.spoof_enable = MOTIONSENSE_SPOOF_MODE_DISABLE;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	params.spoof.spoof_enable = MOTIONSENSE_SPOOF_MODE_QUERY;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	zassert_equal(response->spoof.ret, 0);

	/* 12. Invalid cmd */
	params.cmd = 0xfe;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
}

ZTEST(motion_sense, test_motion_sense_console_commands)
{
	/* accelrange */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrange 0 2 1"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrange 0"));
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelrange 99"),
			  0);
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelrange"), 0);

	/* accelres */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelres 0 0 1"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelres 0"));
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelres 99"), 0);
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelres"), 0);

	/* accelrate */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrate 0 10000 1"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelrate 0"));
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelrate 99"), 0);
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelrate"), 0);

	/* accelread */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelread 0 1"));
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelread 99"), 0);
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelread"), 0);

	/* accelinit */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelinit 0"));
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelinit 99"), 0);
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelinit"), 0);

	/* accelinfo */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelinfo"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelinfo on"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelinfo off"));
	zassert_not_equal(
		shell_execute_cmd(get_ec_shell(), "accelinfo invalid"), 0);
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelinfo 1 2 3"),
			  0);

	/* accelspoof */
	zassert_ok(shell_execute_cmd(get_ec_shell(),
				     "accelspoof 0 on 100 200 300"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelspoof 0 on"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelspoof 0 off"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelspoof 0"));
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelspoof 99"),
			  0);
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "accelspoof"), 0);
}

ZTEST(motion_sense, test_motion_sense_host_commands_extended)
{
	struct ec_params_motion_sense params;
	uint8_t response_buffer[256];
	struct ec_response_motion_sense *response =
		(struct ec_response_motion_sense *)response_buffer;
	struct host_cmd_handler_args args = {
		.command = EC_CMD_MOTION_SENSE_CMD,
		.version = 4,
		.params = &params,
		.params_size = sizeof(params),
		.response = response,
		.response_max = sizeof(response_buffer),
	};
	enum sensor_state orig_state;

	/* MOTIONSENSE_CMD_INFO version 1, 2, 3, 4 */
	for (int ver = 1; ver <= 4; ver++) {
		args.version = ver;
		memset(&params, 0, sizeof(params));
		params.cmd = MOTIONSENSE_CMD_INFO;
		params.sensor_odr.sensor_num = 0;
		zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	}

	/* SENSOR_NOT_INITIALIZED / SENSOR_INITIALIZED -> EC_RES_BUSY */
	orig_state = motion_sensors[0].state;
	motion_sensors[0].state = SENSOR_INITIALIZED;
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_INFO;
	params.sensor_odr.sensor_num = 0;
	zassert_equal(host_command_process(&args), EC_RES_BUSY);

	motion_sensors[0].state = SENSOR_INIT_ERROR;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);

	motion_sensors[0].state = orig_state;

	/* MOTIONSENSE_CMD_ONLINE_CALIB_READ -> EC_RES_INVALID_PARAM */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_ONLINE_CALIB_READ;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
}
