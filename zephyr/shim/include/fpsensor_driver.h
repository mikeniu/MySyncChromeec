<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_INCLUDE_FPSENSOR_DRIVER_H__
#define __ZEPHYR_SHIM_INCLUDE_FPSENSOR_DRIVER_H__

#include <drivers/fingerprint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_FINGERPRINT_SENSOR_IMAGE_OFFSET
#define FP_SENSOR_IMAGE_OFFSET CONFIG_FINGERPRINT_SENSOR_IMAGE_OFFSET
#else
#define FP_SENSOR_IMAGE_OFFSET 0
#endif

#ifdef CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE
#define FP_SENSOR_IMAGE_SIZE CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE
#else
#define FP_SENSOR_IMAGE_SIZE \
	MAX_FRAME_SIZE(DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif

/*
 * Tell fpsensor code that private driver is present, even if this is a public
 * build. If the build is public, we will provide mocks.
 */
#define HAVE_FP_PRIVATE_DRIVER

#ifdef __cplusplus
}
#endif

#endif /* __ZEPHYR_SHIM_INCLUDE_FPSENSOR_DRIVER_H__ */
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_INCLUDE_FPSENSOR_DRIVER_H__
#define __ZEPHYR_SHIM_INCLUDE_FPSENSOR_DRIVER_H__

#include <drivers/fingerprint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_FINGERPRINT_SENSOR_IMAGE_OFFSET
#define FP_SENSOR_IMAGE_OFFSET CONFIG_FINGERPRINT_SENSOR_IMAGE_OFFSET
#else
#define FP_SENSOR_IMAGE_OFFSET 0
#endif

#ifdef CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE
#define FP_SENSOR_IMAGE_SIZE CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE
#else
#define FP_SENSOR_IMAGE_SIZE                \
	FINGERPRINT_SENSOR_REAL_IMAGE_SIZE( \
		DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif

#define FP_SENSOR_RES_X \
	FINGERPRINT_SENSOR_RES_X(DT_CHOSEN(cros_fp_fingerprint_sensor))
#define FP_SENSOR_RES_Y \
	FINGERPRINT_SENSOR_RES_Y(DT_CHOSEN(cros_fp_fingerprint_sensor))

/*
 * Tell fpsensor code that private driver is present, even if this is a public
 * build. If the build is public, we will provide mocks.
 */
#define HAVE_FP_PRIVATE_DRIVER

#ifdef __cplusplus
}
#endif

#endif /* __ZEPHYR_SHIM_INCLUDE_FPSENSOR_DRIVER_H__ */
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
