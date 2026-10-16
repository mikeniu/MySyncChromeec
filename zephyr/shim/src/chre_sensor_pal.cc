/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#ifndef CONFIG_ZTEST
#include <algorithm>

using std::max;
using std::min;
#endif

#include "motion_sense.h"

extern "C" bool chre_is_ready(void);

#include "chre/pal/sensor.h"
#include "chre/platform/log.h"
#include "chre/platform/memory.h"
#include "chre/platform/system_time.h"
#include "chre/util/macros.h"

namespace
{

constexpr uint32_t kMaxPalSensors = MAX_MOTION_SENSORS * 2 + 2;
constexpr float kGravityEarth = 9.80665f;
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kRawFullScale = 32768.0f;
constexpr float kMotionDetectThresholdMps2 = 0.5f;

struct PalSensorContext {
	int ecSensorNum;
	uint8_t chreSensorType;
	bool isOnChange;
	bool isOneShot;
	bool reportsBias;
	bool enabled;
	bool passive;
	bool biasEventsEnabled;
	bool firstSamplePending;
	float lastLightLux;
	float prevAccel[3];
	uint64_t intervalNs;
	uint64_t latencyNs;
	uint64_t lastSampleTimeNs;
	char uncalName[24];
};

const struct chrePalSystemApi *gSystemApi = nullptr;
const struct chrePalSensorCallbacks *gCallbacks = nullptr;
bool gPalOpen = false;
uint32_t gPalSensorCount = 0;
struct chreSensorInfo gPalSensorInfos[kMaxPalSensors];
PalSensorContext gPalSensors[kMaxPalSensors];
uint32_t gFlushRequestId = 0;
K_MUTEX_DEFINE(gSensorPalMutex);

void populatePalSensorsOnce()
{
	if (gPalSensorCount > 0) {
		return;
	}

	int firstAccelSensorNum = -1;

	for (unsigned int i = 0;
	     i < motion_sensor_count && gPalSensorCount < kMaxPalSensors; i++) {
		const struct motion_sensor_t *ecSensor = &motion_sensors[i];
		uint64_t minIntervalNs = 10000000ULL; /* 100 Hz default */

		if (ecSensor->max_frequency > 0) {
			minIntervalNs =
				1000000000000ULL /
				static_cast<uint64_t>(ecSensor->max_frequency);
		}

		if (ecSensor->type == MOTIONSENSE_TYPE_ACCEL) {
			if (firstAccelSensorNum < 0) {
				firstAccelSensorNum = static_cast<int>(i);
			}
			uint32_t idx = gPalSensorCount++;
			gPalSensors[idx] = {};
			gPalSensors[idx].ecSensorNum = static_cast<int>(i);
			gPalSensors[idx].chreSensorType =
				CHRE_SENSOR_TYPE_ACCELEROMETER;
			gPalSensors[idx].reportsBias = true;

			gPalSensorInfos[idx] = {
				.sensorName = ecSensor->name,
				.sensorType = CHRE_SENSOR_TYPE_ACCELEROMETER,
				.isOnChange = 0,
				.isOneShot = 0,
				.reportsBiasEvents = 1,
				.supportsPassiveMode = 1,
				.unusedFlags = 0,
				.minInterval = minIntervalNs,
				.sensorIndex = CHRE_SENSOR_INDEX_DEFAULT,
			};

			if (gPalSensorCount < kMaxPalSensors) {
				uint32_t uIdx = gPalSensorCount++;
				gPalSensors[uIdx] = {};
				gPalSensors[uIdx].ecSensorNum = i;
				gPalSensors[uIdx].chreSensorType =
					CHRE_SENSOR_TYPE_UNCALIBRATED_ACCELEROMETER;
				gPalSensors[uIdx].reportsBias = true;
				snprintf(gPalSensors[uIdx].uncalName,
					 sizeof(gPalSensors[uIdx].uncalName),
					 "%s_uncal", ecSensor->name);

				gPalSensorInfos[uIdx] = {
					.sensorName =
						gPalSensors[uIdx].uncalName,
					.sensorType =
						CHRE_SENSOR_TYPE_UNCALIBRATED_ACCELEROMETER,
					.isOnChange = 0,
					.isOneShot = 0,
					.reportsBiasEvents = 1,
					.supportsPassiveMode = 1,
					.unusedFlags = 0,
					.minInterval = minIntervalNs,
					.sensorIndex =
						CHRE_SENSOR_INDEX_DEFAULT,
				};
			}
		} else if (ecSensor->type == MOTIONSENSE_TYPE_GYRO) {
			uint32_t idx = gPalSensorCount++;
			gPalSensors[idx] = {};
			gPalSensors[idx].ecSensorNum = i;
			gPalSensors[idx].chreSensorType =
				CHRE_SENSOR_TYPE_GYROSCOPE;
			gPalSensors[idx].reportsBias = true;

			gPalSensorInfos[idx] = {
				.sensorName = ecSensor->name,
				.sensorType = CHRE_SENSOR_TYPE_GYROSCOPE,
				.isOnChange = 0,
				.isOneShot = 0,
				.reportsBiasEvents = 1,
				.supportsPassiveMode = 1,
				.unusedFlags = 0,
				.minInterval = minIntervalNs,
				.sensorIndex = CHRE_SENSOR_INDEX_DEFAULT,
			};

			if (gPalSensorCount < kMaxPalSensors) {
				uint32_t uIdx = gPalSensorCount++;
				gPalSensors[uIdx] = {};
				gPalSensors[uIdx].ecSensorNum = i;
				gPalSensors[uIdx].chreSensorType =
					CHRE_SENSOR_TYPE_UNCALIBRATED_GYROSCOPE;
				gPalSensors[uIdx].reportsBias = true;
				snprintf(gPalSensors[uIdx].uncalName,
					 sizeof(gPalSensors[uIdx].uncalName),
					 "%s_uncal", ecSensor->name);

				gPalSensorInfos[uIdx] = {
					.sensorName =
						gPalSensors[uIdx].uncalName,
					.sensorType =
						CHRE_SENSOR_TYPE_UNCALIBRATED_GYROSCOPE,
					.isOnChange = 0,
					.isOneShot = 0,
					.reportsBiasEvents = 1,
					.supportsPassiveMode = 1,
					.unusedFlags = 0,
					.minInterval = minIntervalNs,
					.sensorIndex =
						CHRE_SENSOR_INDEX_DEFAULT,
				};
			}
		} else if (ecSensor->type == MOTIONSENSE_TYPE_LIGHT) {
			uint32_t idx = gPalSensorCount++;
			gPalSensors[idx] = {};
			gPalSensors[idx].ecSensorNum = i;
			gPalSensors[idx].chreSensorType =
				CHRE_SENSOR_TYPE_LIGHT;
			gPalSensors[idx].isOnChange = true;
			gPalSensors[idx].reportsBias = false;

			gPalSensorInfos[idx] = {
				.sensorName = ecSensor->name,
				.sensorType = CHRE_SENSOR_TYPE_LIGHT,
				.isOnChange = 1,
				.isOneShot = 0,
				.reportsBiasEvents = 0,
				.supportsPassiveMode = 1,
				.unusedFlags = 0,
				.minInterval = 1000000ULL,
				.sensorIndex = CHRE_SENSOR_INDEX_DEFAULT,
			};
		}
	}

	if (firstAccelSensorNum >= 0) {
		if (gPalSensorCount < kMaxPalSensors) {
			uint32_t idx = gPalSensorCount++;
			gPalSensors[idx] = {};
			gPalSensors[idx].ecSensorNum = firstAccelSensorNum;
			gPalSensors[idx].chreSensorType =
				CHRE_SENSOR_TYPE_INSTANT_MOTION_DETECT;
			gPalSensors[idx].isOneShot = true;

			gPalSensorInfos[idx] = {
				.sensorName = "instant-motion",
				.sensorType =
					CHRE_SENSOR_TYPE_INSTANT_MOTION_DETECT,
				.isOnChange = 0,
				.isOneShot = 1,
				.reportsBiasEvents = 0,
				.supportsPassiveMode = 1,
				.unusedFlags = 0,
				.minInterval = CHRE_SENSOR_INTERVAL_DEFAULT,
				.sensorIndex = CHRE_SENSOR_INDEX_DEFAULT,
			};
		}
		if (gPalSensorCount < kMaxPalSensors) {
			uint32_t idx = gPalSensorCount++;
			gPalSensors[idx] = {};
			gPalSensors[idx].ecSensorNum = firstAccelSensorNum;
			gPalSensors[idx].chreSensorType =
				CHRE_SENSOR_TYPE_STATIONARY_DETECT;
			gPalSensors[idx].isOneShot = true;

			gPalSensorInfos[idx] = {
				.sensorName = "stationary-detect",
				.sensorType =
					CHRE_SENSOR_TYPE_STATIONARY_DETECT,
				.isOnChange = 0,
				.isOneShot = 1,
				.reportsBiasEvents = 0,
				.supportsPassiveMode = 1,
				.unusedFlags = 0,
				.minInterval = CHRE_SENSOR_INTERVAL_DEFAULT,
				.sensorIndex = CHRE_SENSOR_INDEX_DEFAULT,
			};
		}
	}
}

void sendZeroBiasEvent(uint32_t sensorInfoIndex)
{
	if (gCallbacks == nullptr ||
	    !gPalSensors[sensorInfoIndex].reportsBias) {
		return;
	}

	auto *bias = static_cast<struct chreSensorThreeAxisData *>(
		chre::memoryAlloc(sizeof(struct chreSensorThreeAxisData)));
	if (bias == nullptr) {
		return;
	}
	memset(bias, 0, sizeof(*bias));
	bias->header.baseTimestamp =
		chre::SystemTime::getMonotonicTime().toRawNanoseconds();
	bias->header.sensorHandle = sensorInfoIndex;
	bias->header.readingCount = 1;
	bias->header.accuracy = CHRE_SENSOR_ACCURACY_HIGH;
	gCallbacks->biasEventCallback(sensorInfoIndex, bias);
}

void updateEcSensorVoteLocked(int ecSensorNum)
{
	uint64_t minIntervalNs = UINT64_MAX;
	bool anyActive = false;

	for (uint32_t i = 0; i < gPalSensorCount; i++) {
		if (gPalSensors[i].ecSensorNum == ecSensorNum &&
		    gPalSensors[i].enabled && !gPalSensors[i].passive) {
			anyActive = true;
			uint64_t interval = gPalSensors[i].intervalNs;
			if (interval == CHRE_SENSOR_INTERVAL_DEFAULT ||
			    interval == 0) {
				interval = 200000000ULL; /* 5 Hz default */
			}
			if (interval < minIntervalNs) {
				minIntervalNs = interval;
			}
		}
	}

	if (!anyActive) {
		motion_sense_set_chre_config(ecSensorNum, 0, 0);
		return;
	}

	const struct motion_sensor_t *ecSensor = &motion_sensors[ecSensorNum];
	uint64_t odrMhz64 = 1000000000000ULL / minIntervalNs;
	if (ecSensor->min_frequency > 0 && odrMhz64 < ecSensor->min_frequency) {
		odrMhz64 = ecSensor->min_frequency;
	}
	if (ecSensor->max_frequency > 0 && odrMhz64 > ecSensor->max_frequency) {
		odrMhz64 = ecSensor->max_frequency;
	}

	unsigned int odrMhz = static_cast<unsigned int>(odrMhz64);
	unsigned int ecRateUs = static_cast<unsigned int>(
		(odrMhz64 > 0) ? (1000000000ULL / odrMhz64) :
				 (minIntervalNs / 1000ULL));
	if (ecRateUs == 0) {
		ecRateUs = 1000;
	}

	motion_sense_set_chre_config(ecSensorNum, odrMhz, ecRateUs);
}

bool chrePalSensorApiOpen(const struct chrePalSystemApi *systemApi,
			  const struct chrePalSensorCallbacks *callbacks)
{
	if (systemApi == nullptr || callbacks == nullptr) {
		return false;
	}

	k_mutex_lock(&gSensorPalMutex, K_FOREVER);
	populatePalSensorsOnce();
	gSystemApi = systemApi;
	gCallbacks = callbacks;
	gPalOpen = true;
	k_mutex_unlock(&gSensorPalMutex);
	return true;
}

void chrePalSensorApiClose(void)
{
	k_mutex_lock(&gSensorPalMutex, K_FOREVER);
	gPalOpen = false;
	for (uint32_t i = 0; i < gPalSensorCount; i++) {
		gPalSensors[i].enabled = false;
		gPalSensors[i].biasEventsEnabled = false;
	}
	for (unsigned int i = 0; i < motion_sensor_count; i++) {
		motion_sense_set_chre_config(static_cast<int>(i), 0, 0);
	}
	k_mutex_unlock(&gSensorPalMutex);
}

bool chrePalSensorApiGetSensors(const struct chreSensorInfo **sensors,
				uint32_t *arraySize)
{
	k_mutex_lock(&gSensorPalMutex, K_FOREVER);
	populatePalSensorsOnce();
	if (sensors != nullptr) {
		*sensors = gPalSensorInfos;
	}
	if (arraySize != nullptr) {
		*arraySize = gPalSensorCount;
	}
	k_mutex_unlock(&gSensorPalMutex);
	return true;
}

bool chrePalSensorApiConfigureSensor(uint32_t sensorInfoIndex,
				     enum chreSensorConfigureMode mode,
				     uint64_t intervalNs, uint64_t latencyNs)
{
	if (sensorInfoIndex >= gPalSensorCount) {
		return false;
	}

	bool enabled = (mode != CHRE_SENSOR_CONFIGURE_MODE_DONE);
	bool passive = (mode == CHRE_SENSOR_CONFIGURE_MODE_PASSIVE_CONTINUOUS ||
			mode == CHRE_SENSOR_CONFIGURE_MODE_PASSIVE_ONE_SHOT);

	k_mutex_lock(&gSensorPalMutex, K_FOREVER);
	PalSensorContext &pal = gPalSensors[sensorInfoIndex];
	pal.enabled = enabled;
	pal.passive = passive;
	pal.intervalNs = enabled ? intervalNs : 0;
	pal.latencyNs = enabled ? latencyNs : 0;
	if (enabled) {
		pal.firstSamplePending = true;
	}

	updateEcSensorVoteLocked(pal.ecSensorNum);
	bool sendBias = enabled && pal.biasEventsEnabled && pal.reportsBias;
#ifndef CONFIG_ZTEST
	bool sendInitialLight = enabled &&
				(pal.chreSensorType == CHRE_SENSOR_TYPE_LIGHT);
	float initialLux =
		sendInitialLight ?
			static_cast<float>(
				motion_sensors[pal.ecSensorNum].raw_xyz[0]) :
			0.0f;
	if (sendInitialLight) {
		pal.firstSamplePending = false;
		pal.lastLightLux = initialLux;
	}
#endif
	k_mutex_unlock(&gSensorPalMutex);

	if (gCallbacks != nullptr) {
		auto *status = static_cast<struct chreSensorSamplingStatus *>(
			chre::memoryAlloc(
				sizeof(struct chreSensorSamplingStatus)));
		if (status != nullptr) {
			status->interval = enabled ? intervalNs : 0;
			status->latency = enabled ? latencyNs : 0;
			status->enabled = enabled;
			gCallbacks->samplingStatusUpdateCallback(
				sensorInfoIndex, status);
		}
		if (sendBias) {
			sendZeroBiasEvent(sensorInfoIndex);
		}
#ifndef CONFIG_ZTEST
		if (sendInitialLight) {
			auto *ev = static_cast<struct chreSensorFloatData *>(
				chre::memoryAlloc(
					sizeof(struct chreSensorFloatData)));
			if (ev != nullptr) {
				memset(ev, 0, sizeof(*ev));
				ev->header.baseTimestamp =
					chre::SystemTime::getMonotonicTime()
						.toRawNanoseconds();
				ev->header.sensorHandle = sensorInfoIndex;
				ev->header.readingCount = 1;
				ev->header.accuracy = CHRE_SENSOR_ACCURACY_HIGH;
				ev->readings[0].timestampDelta = 0;
				ev->readings[0].value = initialLux;
				gCallbacks->dataEventCallback(sensorInfoIndex,
							      ev);
			}
		}
#endif
	}

	return true;
}

bool chrePalSensorApiFlush(uint32_t sensorInfoIndex, uint32_t *flushRequestId)
{
	if (sensorInfoIndex >= gPalSensorCount || gCallbacks == nullptr) {
		return false;
	}

	uint32_t reqId = ++gFlushRequestId;
	if (flushRequestId != nullptr) {
		*flushRequestId = reqId;
	}
	gCallbacks->flushCompleteCallback(sensorInfoIndex, reqId,
					  CHRE_ERROR_NONE);
	return true;
}

bool chrePalSensorApiConfigureBiasEvents(uint32_t sensorInfoIndex, bool enable,
					 uint64_t latencyNs)
{
	UNUSED_VAR(latencyNs);
	if (sensorInfoIndex >= gPalSensorCount) {
		return false;
	}

	k_mutex_lock(&gSensorPalMutex, K_FOREVER);
	if (!gPalSensors[sensorInfoIndex].reportsBias) {
		k_mutex_unlock(&gSensorPalMutex);
		return false;
	}
	gPalSensors[sensorInfoIndex].biasEventsEnabled = enable;
	bool shouldSend = enable && gPalSensors[sensorInfoIndex].enabled;
	k_mutex_unlock(&gSensorPalMutex);

	if (shouldSend) {
		sendZeroBiasEvent(sensorInfoIndex);
	}
	return true;
}

bool chrePalSensorApiGetThreeAxisBias(uint32_t sensorInfoIndex,
				      struct chreSensorThreeAxisData *bias)
{
	if (sensorInfoIndex >= gPalSensorCount || bias == nullptr ||
	    !gPalSensors[sensorInfoIndex].reportsBias) {
		return false;
	}

	memset(bias, 0, sizeof(*bias));
	bias->header.baseTimestamp =
		chre::SystemTime::getMonotonicTime().toRawNanoseconds();
	bias->header.sensorHandle = sensorInfoIndex;
	bias->header.readingCount = 1;
	bias->header.accuracy = CHRE_SENSOR_ACCURACY_HIGH;
	return true;
}

void chrePalSensorApiReleaseSensorDataEvent(void *data)
{
	chre::memoryFree(data);
}

void chrePalSensorApiReleaseSamplingStatusEvent(
	struct chreSensorSamplingStatus *status)
{
	chre::memoryFree(status);
}

void chrePalSensorApiReleaseBiasEvent(void *bias)
{
	chre::memoryFree(bias);
}

uint32_t chrePalSensorApiGetCapabilities()
{
	return CHRE_SENSOR_CAPABILITIES_NONE;
}

} // namespace

extern "C" void
chre_sensor_pal_on_sample(const struct ec_response_motion_sensor_data *data,
			  const struct motion_sensor_t *sensor, int valid_data,
			  uint32_t time)
{
	ARG_UNUSED(time);

	if (!chre_is_ready() || !gPalOpen || gCallbacks == nullptr ||
	    data == nullptr || sensor == nullptr || valid_data <= 0) {
		return;
	}

	uint64_t nowNs =
		chre::SystemTime::getMonotonicTime().toRawNanoseconds();
	int ecSensorNum = data->sensor_num;

	for (uint32_t i = 0; i < gPalSensorCount; i++) {
		PalSensorContext &pal = gPalSensors[i];
		if (pal.ecSensorNum != ecSensorNum || !pal.enabled) {
			continue;
		}

		if (!pal.isOnChange && pal.intervalNs > 0 &&
		    pal.lastSampleTimeNs > 0 && nowNs > pal.lastSampleTimeNs &&
		    (nowNs - pal.lastSampleTimeNs) < (pal.intervalNs * 3 / 4)) {
			continue;
		}

		uint64_t sampleTs = nowNs;
		if (sampleTs <= pal.lastSampleTimeNs) {
			sampleTs = pal.lastSampleTimeNs + 1000ULL;
		}

		if (pal.isOneShot) {
			float scale =
				static_cast<float>(sensor->current_range) *
				kGravityEarth / kRawFullScale;
			float x = static_cast<float>(data->data[0]) * scale;
			float y = static_cast<float>(data->data[1]) * scale;
			float z = static_cast<float>(data->data[2]) * scale;

			if (pal.firstSamplePending) {
				pal.prevAccel[0] = x;
				pal.prevAccel[1] = y;
				pal.prevAccel[2] = z;
				pal.firstSamplePending = false;
				pal.lastSampleTimeNs = sampleTs;
				continue;
			}

			float delta = fabsf(x - pal.prevAccel[0]) +
				      fabsf(y - pal.prevAccel[1]) +
				      fabsf(z - pal.prevAccel[2]);
			pal.prevAccel[0] = x;
			pal.prevAccel[1] = y;
			pal.prevAccel[2] = z;

			bool triggered =
				(pal.chreSensorType ==
				 CHRE_SENSOR_TYPE_INSTANT_MOTION_DETECT) ?
					(delta > kMotionDetectThresholdMps2) :
					(delta <= kMotionDetectThresholdMps2);
			if (!triggered) {
				pal.lastSampleTimeNs = sampleTs;
				continue;
			}

			auto *ev = static_cast<struct chreSensorOccurrenceData *>(
				chre::memoryAlloc(sizeof(
					struct chreSensorOccurrenceData)));
			if (ev == nullptr) {
				continue;
			}
			memset(ev, 0, sizeof(*ev));
			ev->header.baseTimestamp = sampleTs;
			ev->header.sensorHandle = i;
			ev->header.readingCount = 1;
			ev->header.accuracy = CHRE_SENSOR_ACCURACY_HIGH;
			ev->readings[0].timestampDelta = 0;

			k_mutex_lock(&gSensorPalMutex, K_FOREVER);
			pal.enabled = false;
			pal.lastSampleTimeNs = sampleTs;
			updateEcSensorVoteLocked(pal.ecSensorNum);
			k_mutex_unlock(&gSensorPalMutex);

			gCallbacks->dataEventCallback(i, ev);
		} else if (sensor->type == MOTIONSENSE_TYPE_ACCEL ||
			   sensor->type == MOTIONSENSE_TYPE_GYRO) {
			float scale = (sensor->type == MOTIONSENSE_TYPE_ACCEL) ?
					      (static_cast<float>(
						       sensor->current_range) *
					       kGravityEarth / kRawFullScale) :
					      (static_cast<float>(
						       sensor->current_range) *
					       kDegToRad / kRawFullScale);

			auto *ev = static_cast<struct chreSensorThreeAxisData *>(
				chre::memoryAlloc(sizeof(
					struct chreSensorThreeAxisData)));
			if (ev == nullptr) {
				continue;
			}
			memset(ev, 0, sizeof(*ev));
			ev->header.baseTimestamp = sampleTs;
			ev->header.sensorHandle = i;
			ev->header.readingCount = 1;
			ev->header.accuracy = CHRE_SENSOR_ACCURACY_HIGH;
			ev->readings[0].timestampDelta = 0;
			ev->readings[0].x =
				static_cast<float>(data->data[0]) * scale;
			ev->readings[0].y =
				static_cast<float>(data->data[1]) * scale;
			ev->readings[0].z =
				static_cast<float>(data->data[2]) * scale;

			pal.lastSampleTimeNs = sampleTs;
			gCallbacks->dataEventCallback(i, ev);
		} else if (sensor->type == MOTIONSENSE_TYPE_LIGHT) {
			float lux = static_cast<float>(data->udata[0]);
			if (!pal.firstSamplePending &&
			    fabsf(lux - pal.lastLightLux) < 0.5f) {
				continue;
			}

			auto *ev = static_cast<struct chreSensorFloatData *>(
				chre::memoryAlloc(
					sizeof(struct chreSensorFloatData)));
			if (ev == nullptr) {
				continue;
			}
			memset(ev, 0, sizeof(*ev));
			ev->header.baseTimestamp = sampleTs;
			ev->header.sensorHandle = i;
			ev->header.readingCount = 1;
			ev->header.accuracy = CHRE_SENSOR_ACCURACY_HIGH;
			ev->readings[0].timestampDelta = 0;
			ev->readings[0].value = lux;

			pal.firstSamplePending = false;
			pal.lastLightLux = lux;
			pal.lastSampleTimeNs = sampleTs;
			gCallbacks->dataEventCallback(i, ev);
		}
	}

#ifndef CONFIG_ZTEST
	for (uint32_t i = 0; i < gPalSensorCount; i++) {
		PalSensorContext &pal = gPalSensors[i];
		if (!pal.enabled ||
		    pal.chreSensorType != CHRE_SENSOR_TYPE_LIGHT) {
			continue;
		}
		if (pal.lastSampleTimeNs > 0 && nowNs > pal.lastSampleTimeNs &&
		    (nowNs - pal.lastSampleTimeNs) < 1000000000ULL) {
			continue;
		}
		float lux = static_cast<float>(
			motion_sensors[pal.ecSensorNum].raw_xyz[0]);
		auto *ev = static_cast<struct chreSensorFloatData *>(
			chre::memoryAlloc(sizeof(struct chreSensorFloatData)));
		if (ev == nullptr) {
			continue;
		}
		memset(ev, 0, sizeof(*ev));
		ev->header.baseTimestamp = nowNs;
		ev->header.sensorHandle = i;
		ev->header.readingCount = 1;
		ev->header.accuracy = CHRE_SENSOR_ACCURACY_HIGH;
		ev->readings[0].timestampDelta = 0;
		ev->readings[0].value = lux;

		pal.firstSamplePending = false;
		pal.lastLightLux = lux;
		pal.lastSampleTimeNs = nowNs;
		gCallbacks->dataEventCallback(i, ev);
	}
#endif
}

const struct chrePalSensorApi *chrePalSensorGetApi(uint32_t requestedApiVersion)
{
	static const struct chrePalSensorApi kApi = {
		.moduleVersion = CHRE_PAL_SENSOR_API_CURRENT_VERSION,
		.open = chrePalSensorApiOpen,
		.close = chrePalSensorApiClose,
		.getSensors = chrePalSensorApiGetSensors,
		.configureSensor = chrePalSensorApiConfigureSensor,
		.flush = chrePalSensorApiFlush,
		.configureBiasEvents = chrePalSensorApiConfigureBiasEvents,
		.getThreeAxisBias = chrePalSensorApiGetThreeAxisBias,
		.releaseSensorDataEvent =
			chrePalSensorApiReleaseSensorDataEvent,
		.releaseSamplingStatusEvent =
			chrePalSensorApiReleaseSamplingStatusEvent,
		.releaseBiasEvent = chrePalSensorApiReleaseBiasEvent,
		.configureSensorAsync = nullptr,
		.getCapabilities = chrePalSensorApiGetCapabilities,
	};

	if (!CHRE_PAL_VERSIONS_ARE_COMPATIBLE(kApi.moduleVersion,
					      requestedApiVersion)) {
		return nullptr;
	}
	return &kApi;
}
