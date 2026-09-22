/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_
#define PLATFORM_EC_ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>

struct fp_simulator_cfg {
	uint8_t *image_buffer;
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params sensor_image_configs[];
};

struct fp_simulator_data {
	fingerprint_callback_t callback;
	struct fingerprint_sensor_state state;
	uint16_t errors;
};

#endif /* PLATFORM_EC_ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_ */
