<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/fingerprint.h"

#include <zephyr/ztest.h>

#ifdef CONFIG_CROS_EC_RW
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#ifdef CONFIG_FINGERPRINT_SENSOR_FPC1025
#define FP_SENSOR_HWID_FPC 0x021
#endif /* CONFIG_FINGERPRINT_SENSOR_FPC1025 */
static const uint32_t fp_sensor_hwid = FP_SENSOR_HWID_FPC;
#else
static const uint32_t fp_sensor_hwid = UINT32_MAX;
#endif

int fpc_get_hwid(uint16_t *id);

ZTEST_SUITE(fpsensor_hw, NULL, NULL, NULL, NULL, NULL);

/* Hardware-dependent smoke test that makes a SPI transaction with the
 * fingerprint sensor.
 */
ZTEST(fpsensor_hw, test_fp_check_hwid)
{
	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		struct fingerprint_sensor_info sensor_info{};
		struct fingerprint_image_frame_params
			image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES] = {};
		uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

		zassert_ok(fingerprint_get_info(fp_sensor_dev, &sensor_info,
						image_frame_params_array,
						&num_params));
		/* The lower 4-bits of the sensor hardware id are a
		 * manufacturing ID that is ok to vary.
		 */
		zassert_equal(fp_sensor_hwid, sensor_info.model_id >> 4);
	};
}
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/fingerprint.h"

#include <zephyr/ztest.h>

#ifdef CONFIG_CROS_EC_RW
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#ifdef CONFIG_FINGERPRINT_SENSOR_FPC1025
#define FP_SENSOR_HWID_FPC 0x021
#endif /* CONFIG_FINGERPRINT_SENSOR_FPC1025 */
static const uint32_t fp_sensor_hwid = FP_SENSOR_HWID_FPC;
#else
static const uint32_t fp_sensor_hwid = UINT32_MAX;
#endif

int fpc_get_hwid(uint16_t *id);

ZTEST_SUITE(fpsernsor_hw, NULL, NULL, NULL, NULL, NULL);

/* Hardware-dependent smoke test that makes a SPI transaction with the
 * fingerprint sensor.
 */
ZTEST(fpsernsor_hw, test_fp_check_hwid)
{
	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		struct fingerprint_info info;

		zassert_ok(fingerprint_get_info(fp_sensor_dev, &info));
		/* The lower 4-bits of the sensor hardware id are a
		 * manufacturing ID that is ok to vary.
		 */
		zassert_equal(fp_sensor_hwid, info.model_id >> 4);
	};
}
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
