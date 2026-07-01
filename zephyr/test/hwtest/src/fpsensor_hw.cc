/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/fingerprint.h"

#include <zephyr/ztest.h>

#ifdef CONFIG_CROS_EC_RW
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))

#if defined(CONFIG_FINGERPRINT_SENSOR_FPC1025)
#define FP_SENSOR_HWID_EXPECTED 0x021
#elif defined(CONFIG_FINGERPRINT_SENSOR_ELAN80SG)
#define FP_SENSOR_HWID_EXPECTED 0x4f4f
#elif defined(CONFIG_FINGERPRINT_SENSOR_ELANI80SA)
#define FP_SENSOR_HWID_EXPECTED 0x5253
#else
#define FP_SENSOR_HWID_EXPECTED 0x0000
#endif /* CONFIG_FINGERPRINT_SENSOR_FPC1025 */

static const uint32_t fp_sensor_hwid = FP_SENSOR_HWID_EXPECTED;
#else
static const uint32_t fp_sensor_hwid = UINT32_MAX;
#endif /* CONFIG_CROS_EC_RW */

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

		uint32_t actual_hwid = sensor_info.model_id;

		/* The lower 4-bits of the FPC sensor hardware id are a
		 * manufacturing ID that is ok to vary.
		 */
		if (IS_ENABLED(CONFIG_FINGERPRINT_SENSOR_FPC1025)) {
			actual_hwid >>= 4;
		}

		zassert_equal(fp_sensor_hwid, actual_hwid,
			      "Expected HWID 0x%x, got 0x%x", fp_sensor_hwid,
			      actual_hwid);
	};
}
