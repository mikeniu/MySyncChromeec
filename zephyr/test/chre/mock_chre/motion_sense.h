/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_MOTION_SENSE_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_MOTION_SENSE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MOTION_SENSORS 8

enum motionsensor_type {
	MOTIONSENSE_TYPE_ACCEL = 0,
	MOTIONSENSE_TYPE_GYRO = 1,
	MOTIONSENSE_TYPE_MAG = 2,
	MOTIONSENSE_TYPE_PROX = 3,
	MOTIONSENSE_TYPE_LIGHT = 4,
};

struct motion_sensor_t {
	const char *name;
	enum motionsensor_type type;
	uint32_t min_frequency;
	uint32_t max_frequency;
	int current_range;
};

struct ec_response_motion_sensor_data {
	uint8_t flags;
	uint8_t activity;
	uint8_t state;
	uint8_t sensor_num;
	union {
		int16_t data[3];
		struct {
			uint16_t reserved;
			uint32_t timestamp;
		};
		uint32_t udata[1];
	};
};

extern struct motion_sensor_t motion_sensors[];
extern const unsigned int motion_sensor_count;

void motion_sense_set_chre_config(int sensor_num, unsigned int odr_mhz,
				  unsigned int ec_rate_us);

void chre_sensor_pal_on_sample(const struct ec_response_motion_sensor_data *d,
			       const struct motion_sensor_t *sensor,
			       int valid_data, uint32_t time);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_TEST_CHRE_MOCK_CHRE_MOTION_SENSE_H_ */
