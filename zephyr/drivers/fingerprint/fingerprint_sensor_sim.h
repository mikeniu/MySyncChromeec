<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_

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

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_ */
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>

struct fp_simulator_cfg {
	struct fingerprint_info info;
	uint8_t *image_buffer;
};

struct fp_simulator_data {
	fingerprint_callback_t callback;
	struct fingerprint_sensor_state state;
	uint16_t errors;
};

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_SENSOR_SIM_H_ */
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
