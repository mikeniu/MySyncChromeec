/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_PAL_SENSOR_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_PAL_SENSOR_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHRE_PAL_SENSOR_API_CURRENT_VERSION UINT32_C(0x01050000)
#define CHRE_PAL_VERSIONS_ARE_COMPATIBLE(a, b) \
	(((a) & UINT32_C(0xFFFF0000)) == ((b) & UINT32_C(0xFFFF0000)))

#define CHRE_SENSOR_TYPE_ACCELEROMETER UINT8_C(1)
#define CHRE_SENSOR_TYPE_INSTANT_MOTION_DETECT UINT8_C(2)
#define CHRE_SENSOR_TYPE_STATIONARY_DETECT UINT8_C(3)
#define CHRE_SENSOR_TYPE_GYROSCOPE UINT8_C(6)
#define CHRE_SENSOR_TYPE_LIGHT UINT8_C(12)
#define CHRE_SENSOR_TYPE_UNCALIBRATED_ACCELEROMETER UINT8_C(55)
#define CHRE_SENSOR_TYPE_UNCALIBRATED_GYROSCOPE UINT8_C(56)

#define CHRE_SENSOR_INDEX_DEFAULT UINT8_C(0)
#define CHRE_SENSOR_INTERVAL_DEFAULT UINT64_C(0xFFFFFFFFFFFFFFFF)
#define CHRE_SENSOR_ACCURACY_HIGH UINT8_C(3)
#define CHRE_SENSOR_CAPABILITIES_NONE UINT32_C(0)
#define CHRE_ERROR_NONE UINT8_C(0)

enum chreSensorConfigureMode {
	CHRE_SENSOR_CONFIGURE_MODE_DONE = 0,
	CHRE_SENSOR_CONFIGURE_MODE_CONTINUOUS = 1,
	CHRE_SENSOR_CONFIGURE_MODE_ONE_SHOT = 2,
	CHRE_SENSOR_CONFIGURE_MODE_PASSIVE_CONTINUOUS = 3,
	CHRE_SENSOR_CONFIGURE_MODE_PASSIVE_ONE_SHOT = 4,
};

struct chreSensorInfo {
	const char *sensorName;
	uint8_t sensorType;
	uint8_t isOnChange : 1;
	uint8_t isOneShot : 1;
	uint8_t reportsBiasEvents : 1;
	uint8_t supportsPassiveMode : 1;
	uint8_t unusedFlags : 4;
	uint64_t minInterval;
	uint8_t sensorIndex;
};

struct chreSensorSamplingStatus {
	uint64_t interval;
	uint64_t latency;
	bool enabled;
};

struct chreSensorDataHeader {
	uint64_t baseTimestamp;
	uint32_t sensorHandle;
	uint16_t readingCount;
	uint8_t accuracy;
	uint8_t reserved;
};

struct chreSensorThreeAxisSampleData {
	uint32_t timestampDelta;
	union {
		float values[3];
		float v[3];
		struct {
			float x;
			float y;
			float z;
		};
		struct {
			float x_bias;
			float y_bias;
			float z_bias;
		};
	};
};

struct chreSensorThreeAxisData {
	struct chreSensorDataHeader header;
	struct chreSensorThreeAxisSampleData readings[1];
};

struct chreSensorFloatSampleData {
	uint32_t timestampDelta;
	union {
		float value;
		float lux;
	};
};

struct chreSensorFloatData {
	struct chreSensorDataHeader header;
	struct chreSensorFloatSampleData readings[1];
};

struct chreSensorOccurrenceSampleData {
	uint32_t timestampDelta;
};

struct chreSensorOccurrenceData {
	struct chreSensorDataHeader header;
	struct chreSensorOccurrenceSampleData readings[1];
};

struct chrePalSystemApi {
	uint32_t version;
};

struct chrePalSensorCallbacks {
	void (*samplingStatusUpdateCallback)(
		uint32_t sensorInfoIndex,
		struct chreSensorSamplingStatus *status);
	void (*dataEventCallback)(uint32_t sensorInfoIndex, void *data);
	void (*biasEventCallback)(uint32_t sensorInfoIndex, void *biasData);
	void (*flushCompleteCallback)(uint32_t sensorInfoIndex,
				      uint32_t flushRequestId,
				      uint8_t errorCode);
};

struct chrePalSensorApi {
	uint32_t moduleVersion;
	bool (*open)(const struct chrePalSystemApi *systemApi,
		     const struct chrePalSensorCallbacks *callbacks);
	void (*close)(void);
	bool (*getSensors)(const struct chreSensorInfo **sensors,
			   uint32_t *arraySize);
	bool (*configureSensor)(uint32_t sensorInfoIndex,
				enum chreSensorConfigureMode mode,
				uint64_t intervalNs, uint64_t latencyNs);
	bool (*flush)(uint32_t sensorInfoIndex, uint32_t *flushRequestId);
	bool (*configureBiasEvents)(uint32_t sensorInfoIndex, bool enable,
				    uint64_t latencyNs);
	bool (*getThreeAxisBias)(uint32_t sensorInfoIndex,
				 struct chreSensorThreeAxisData *bias);
	void (*releaseSensorDataEvent)(void *data);
	void (*releaseSamplingStatusEvent)(
		struct chreSensorSamplingStatus *status);
	void (*releaseBiasEvent)(void *bias);
	bool (*configureSensorAsync)(uint32_t sensorInfoIndex,
				     enum chreSensorConfigureMode mode,
				     uint64_t intervalNs, uint64_t latencyNs);
	uint32_t (*getCapabilities)(void);
};

const struct chrePalSensorApi *
chrePalSensorGetApi(uint32_t requestedApiVersion);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_TEST_CHRE_MOCK_CHRE_PAL_SENSOR_H_ */
