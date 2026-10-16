/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chre/pal/sensor.h"
#include "chre/platform/memory.h"
#include "chre/platform/system_time.h"
#include "motion_sense.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/ztest.h>

extern "C" void chre_set_ready_for_test(bool ready);

struct motion_sensor_t motion_sensors[] = {
	{
		.name = "lid-accel",
		.type = MOTIONSENSE_TYPE_ACCEL,
		.min_frequency = 12500,
		.max_frequency = 200000,
		.current_range = 2,
	},
	{
		.name = "lid-gyro",
		.type = MOTIONSENSE_TYPE_GYRO,
		.min_frequency = 25000,
		.max_frequency = 200000,
		.current_range = 1000,
	},
	{
		.name = "lid-als",
		.type = MOTIONSENSE_TYPE_LIGHT,
		.min_frequency = 100,
		.max_frequency = 10000,
		.current_range = 1,
	},
};

const unsigned int motion_sensor_count =
	sizeof(motion_sensors) / sizeof(motion_sensors[0]);

namespace
{

struct EcVote {
	unsigned int odr_mhz;
	unsigned int ec_rate_us;
	uint32_t call_count;
};

EcVote g_ec_votes[3];
uint64_t g_mock_time_ns = 1000000000ULL;

uint32_t g_last_status_idx = UINT32_MAX;
struct chreSensorSamplingStatus g_last_status;
uint32_t g_status_count;

uint32_t g_last_data_idx = UINT32_MAX;
struct chreSensorThreeAxisData g_last_three_axis;
struct chreSensorFloatData g_last_float_data;
uint32_t g_data_count;

uint32_t g_last_bias_idx = UINT32_MAX;
struct chreSensorThreeAxisData g_last_bias;
uint32_t g_bias_count;

uint32_t g_last_flush_idx = UINT32_MAX;
uint32_t g_last_flush_req_id;
uint8_t g_last_flush_err = 0xFF;
uint32_t g_flush_count;

const struct chrePalSensorApi *g_api;

void onSamplingStatusUpdate(uint32_t sensorInfoIndex,
			    struct chreSensorSamplingStatus *status)
{
	g_last_status_idx = sensorInfoIndex;
	if (status != nullptr) {
		g_last_status = *status;
		g_api->releaseSamplingStatusEvent(status);
	}
	g_status_count++;
}

void onDataEvent(uint32_t sensorInfoIndex, void *data)
{
	g_last_data_idx = sensorInfoIndex;
	if (data != nullptr) {
		if (sensorInfoIndex == 4) {
			g_last_float_data =
				*static_cast<const struct chreSensorFloatData *>(
					data);
		} else {
			g_last_three_axis = *static_cast<
				const struct chreSensorThreeAxisData *>(data);
		}
		g_api->releaseSensorDataEvent(data);
	}
	g_data_count++;
}

void onBiasEvent(uint32_t sensorInfoIndex, void *biasData)
{
	g_last_bias_idx = sensorInfoIndex;
	if (biasData != nullptr) {
		g_last_bias =
			*static_cast<const struct chreSensorThreeAxisData *>(
				biasData);
		g_api->releaseBiasEvent(biasData);
	}
	g_bias_count++;
}

void onFlushComplete(uint32_t sensorInfoIndex, uint32_t flushRequestId,
		     uint8_t errorCode)
{
	g_last_flush_idx = sensorInfoIndex;
	g_last_flush_req_id = flushRequestId;
	g_last_flush_err = errorCode;
	g_flush_count++;
}

const struct chrePalSystemApi kSystemApi = {
	.version = 1,
};

const struct chrePalSensorCallbacks kCallbacks = {
	.samplingStatusUpdateCallback = onSamplingStatusUpdate,
	.dataEventCallback = onDataEvent,
	.biasEventCallback = onBiasEvent,
	.flushCompleteCallback = onFlushComplete,
};

void resetTestState()
{
	memset(g_ec_votes, 0, sizeof(g_ec_votes));
	g_mock_time_ns = 1000000000ULL;
	g_last_status_idx = UINT32_MAX;
	memset(&g_last_status, 0, sizeof(g_last_status));
	g_status_count = 0;
	g_last_data_idx = UINT32_MAX;
	memset(&g_last_three_axis, 0, sizeof(g_last_three_axis));
	memset(&g_last_float_data, 0, sizeof(g_last_float_data));
	g_data_count = 0;
	g_last_bias_idx = UINT32_MAX;
	memset(&g_last_bias, 0, sizeof(g_last_bias));
	g_bias_count = 0;
	g_last_flush_idx = UINT32_MAX;
	g_last_flush_req_id = 0;
	g_last_flush_err = 0xFF;
	g_flush_count = 0;
	chre_set_ready_for_test(true);
}

} // namespace

extern "C" void motion_sense_set_chre_config(int sensor_num,
					     unsigned int odr_mhz,
					     unsigned int ec_rate_us)
{
	if (sensor_num >= 0 && sensor_num < 3) {
		g_ec_votes[sensor_num].odr_mhz = odr_mhz;
		g_ec_votes[sensor_num].ec_rate_us = ec_rate_us;
		g_ec_votes[sensor_num].call_count++;
	}
}

namespace chre
{

void *memoryAlloc(size_t size)
{
	return malloc(size);
}

void memoryFree(void *pointer)
{
	free(pointer);
}

Nanoseconds SystemTime::getMonotonicTime()
{
	return Nanoseconds(g_mock_time_ns);
}

} // namespace chre

ZTEST_SUITE(chre_sensor_pal, NULL, NULL, NULL, NULL, NULL);

ZTEST(chre_sensor_pal, test_sensor_pal_open_and_discovery)
{
	resetTestState();

	zassert_is_null(chrePalSensorGetApi(0x02000000),
			"Incompatible major version should return nullptr");

	g_api = chrePalSensorGetApi(CHRE_PAL_SENSOR_API_CURRENT_VERSION);
	zassert_not_null(g_api, "Valid version should return PAL API");

	zassert_false(g_api->open(nullptr, &kCallbacks));
	zassert_false(g_api->open(&kSystemApi, nullptr));
	zassert_true(g_api->open(&kSystemApi, &kCallbacks));

	const struct chreSensorInfo *sensors = nullptr;
	uint32_t count = 0;
	zassert_true(g_api->getSensors(&sensors, &count));
	zassert_not_null(sensors);
	zassert_equal(count, 7, "Expected 7 CHRE sensors, got %u", count);

	zassert_equal(sensors[0].sensorType, CHRE_SENSOR_TYPE_ACCELEROMETER);
	zassert_str_equal(sensors[0].sensorName, "lid-accel");
	zassert_equal(sensors[0].isOnChange, 0);
	zassert_equal(sensors[0].reportsBiasEvents, 1);
	zassert_equal(sensors[0].minInterval, 5000000ULL);

	zassert_equal(sensors[1].sensorType,
		      CHRE_SENSOR_TYPE_UNCALIBRATED_ACCELEROMETER);
	zassert_str_equal(sensors[1].sensorName, "lid-accel_uncal");

	zassert_equal(sensors[2].sensorType, CHRE_SENSOR_TYPE_GYROSCOPE);
	zassert_str_equal(sensors[2].sensorName, "lid-gyro");

	zassert_equal(sensors[3].sensorType,
		      CHRE_SENSOR_TYPE_UNCALIBRATED_GYROSCOPE);
	zassert_str_equal(sensors[3].sensorName, "lid-gyro_uncal");

	zassert_equal(sensors[4].sensorType, CHRE_SENSOR_TYPE_LIGHT);
	zassert_str_equal(sensors[4].sensorName, "lid-als");
	zassert_equal(sensors[4].isOnChange, 1);
	zassert_equal(sensors[4].reportsBiasEvents, 0);

	zassert_equal(sensors[5].sensorType,
		      CHRE_SENSOR_TYPE_INSTANT_MOTION_DETECT);
	zassert_equal(sensors[5].isOneShot, 1);

	zassert_equal(sensors[6].sensorType,
		      CHRE_SENSOR_TYPE_STATIONARY_DETECT);
	zassert_equal(sensors[6].isOneShot, 1);

	g_api->close();
}

ZTEST(chre_sensor_pal, test_sensor_pal_configure_and_ec_voting)
{
	resetTestState();
	g_api = chrePalSensorGetApi(CHRE_PAL_SENSOR_API_CURRENT_VERSION);
	zassert_true(g_api->open(&kSystemApi, &kCallbacks));

	/* Configure calibrated accel at 50 Hz (20 ms interval, 40 ms latency)
	 */
	zassert_true(
		g_api->configureSensor(0, CHRE_SENSOR_CONFIGURE_MODE_CONTINUOUS,
				       20000000ULL, 40000000ULL));
	zassert_equal(g_status_count, 1);
	zassert_equal(g_last_status_idx, 0);
	zassert_true(g_last_status.enabled);
	zassert_equal(g_last_status.interval, 20000000ULL);
	zassert_equal(g_last_status.latency, 40000000ULL);
	zassert_equal(g_ec_votes[0].odr_mhz, 50000U);
	zassert_equal(g_ec_votes[0].ec_rate_us, 20000U);

	/* Configure uncalibrated accel at 100 Hz (10 ms interval): EC vote
	 * must increase to the max ODR (100 Hz = 100000 mHz).
	 */
	zassert_true(
		g_api->configureSensor(1, CHRE_SENSOR_CONFIGURE_MODE_CONTINUOUS,
				       10000000ULL, 10000000ULL));
	zassert_equal(g_ec_votes[0].odr_mhz, 100000U);
	zassert_equal(g_ec_votes[0].ec_rate_us, 10000U);

	/* Disable uncalibrated accel: EC vote falls back to 50 Hz */
	zassert_true(g_api->configureSensor(1, CHRE_SENSOR_CONFIGURE_MODE_DONE,
					    0, 0));
	zassert_equal(g_ec_votes[0].odr_mhz, 50000U);

	/* Disable calibrated accel: EC vote drops to 0 */
	zassert_true(g_api->configureSensor(0, CHRE_SENSOR_CONFIGURE_MODE_DONE,
					    0, 0));
	zassert_equal(g_ec_votes[0].odr_mhz, 0U);
	zassert_equal(g_ec_votes[0].ec_rate_us, 0U);
	zassert_false(g_last_status.enabled);

	g_api->close();
}

ZTEST(chre_sensor_pal, test_sensor_pal_sample_conversion_and_dedup)
{
	resetTestState();
	g_api = chrePalSensorGetApi(CHRE_PAL_SENSOR_API_CURRENT_VERSION);
	zassert_true(g_api->open(&kSystemApi, &kCallbacks));

	/* Enable calibrated accel (idx 0) and ALS light (idx 4) */
	zassert_true(g_api->configureSensor(
		0, CHRE_SENSOR_CONFIGURE_MODE_CONTINUOUS, 10000000ULL, 0));
	zassert_true(g_api->configureSensor(
		4, CHRE_SENSOR_CONFIGURE_MODE_CONTINUOUS, 100000000ULL, 0));

	/* Feed an accel sample: 16384 at 2g full-scale (32768) = +1g (9.80665)
	 */
	struct ec_response_motion_sensor_data accel_sample = {};
	accel_sample.sensor_num = 0;
	accel_sample.data[0] = 16384;
	accel_sample.data[1] = -16384;
	accel_sample.data[2] = 0;

	g_mock_time_ns += 20000000ULL;
	chre_sensor_pal_on_sample(&accel_sample, &motion_sensors[0], 1, 0);

	zassert_equal(g_data_count, 1);
	zassert_equal(g_last_data_idx, 0);
	zassert_within(g_last_three_axis.readings[0].x, 9.80665f, 0.001f);
	zassert_within(g_last_three_axis.readings[0].y, -9.80665f, 0.001f);
	zassert_within(g_last_three_axis.readings[0].z, 0.0f, 0.001f);

	/* Feed an ALS sample (250 lux) */
	struct ec_response_motion_sensor_data als_sample = {};
	als_sample.sensor_num = 2;
	als_sample.udata[0] = 250;

	g_mock_time_ns += 20000000ULL;
	chre_sensor_pal_on_sample(&als_sample, &motion_sensors[2], 1, 0);
	zassert_equal(g_data_count, 2);
	zassert_equal(g_last_data_idx, 4);
	zassert_within(g_last_float_data.readings[0].value, 250.0f, 0.01f);

	/* Duplicate ALS sample (250 lux) should be suppressed for on-change */
	g_mock_time_ns += 20000000ULL;
	chre_sensor_pal_on_sample(&als_sample, &motion_sensors[2], 1, 0);
	zassert_equal(g_data_count, 2,
		      "Unchanged ALS sample should be deduplicated");

	/* Changed ALS sample (300 lux) should be delivered */
	als_sample.udata[0] = 300;
	g_mock_time_ns += 20000000ULL;
	chre_sensor_pal_on_sample(&als_sample, &motion_sensors[2], 1, 0);
	zassert_equal(g_data_count, 3);
	zassert_within(g_last_float_data.readings[0].value, 300.0f, 0.01f);

	g_api->close();
}

ZTEST(chre_sensor_pal, test_sensor_pal_bias_and_flush)
{
	resetTestState();
	g_api = chrePalSensorGetApi(CHRE_PAL_SENSOR_API_CURRENT_VERSION);
	zassert_true(g_api->open(&kSystemApi, &kCallbacks));

	zassert_true(g_api->configureSensor(
		0, CHRE_SENSOR_CONFIGURE_MODE_CONTINUOUS, 20000000ULL, 0));

	/* Bias events supported on 3-axis sensors, rejected on ALS */
	zassert_true(g_api->configureBiasEvents(0, true, 0));
	zassert_equal(g_bias_count, 1);
	zassert_equal(g_last_bias_idx, 0);
	zassert_false(g_api->configureBiasEvents(4, true, 0));

	struct chreSensorThreeAxisData bias = {};
	zassert_true(g_api->getThreeAxisBias(0, &bias));
	zassert_equal(bias.header.accuracy, CHRE_SENSOR_ACCURACY_HIGH);
	zassert_false(g_api->getThreeAxisBias(4, &bias));

	/* Flush request */
	uint32_t req_id = 0;
	zassert_true(g_api->flush(0, &req_id));
	zassert_true(req_id > 0);
	zassert_equal(g_flush_count, 1);
	zassert_equal(g_last_flush_idx, 0);
	zassert_equal(g_last_flush_req_id, req_id);
	zassert_equal(g_last_flush_err, CHRE_ERROR_NONE);

	g_api->close();
}
