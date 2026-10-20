/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_test_utils.h"
#include "mock_fingerprint_algorithm.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <fpsensor/fpsensor.h>
#include <fpsensor/fpsensor_detect.h>
#include <mkbp_event.h>
#include <vector>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

namespace
{

FpSensorInfoBuffer test_info_buffer;

const fingerprint_image_frame_params *
get_frame_params(enum fingerprint_capture_type capture_type)
{
	for (const auto &p : kDefaultSimImageFrameParams) {
		if (p.fp_capture_type == capture_type) {
			return &p;
		}
	}
	return nullptr;
}

void assert_acquire_image_with_size_modifier(ssize_t size_modifier,
					     int expected_result)
{
	const fingerprint_image_frame_params *params =
		get_frame_params(FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE);
	zassert_not_null(params);

	const size_t req_size =
		params->image_data_offset_bytes + params->frame_size;
	const size_t test_size = req_size + size_modifier;
	std::vector<uint8_t> image_buf(MAX(req_size, test_size));

	zassert_equal(fingerprint_acquire_image(
			      fp_sim, FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE,
			      image_buf.data(), test_size),
		      expected_result);
}

void fpsensor_before(void *f)
{
	reset_sim_state();
	reset_mock_alg_fakes();
}

} /* namespace */

ZTEST_USER(fpsensor_shim, test_shim_sensor_type_elan)
{
	const gpio_dt_spec *sensor_sel_pin =
		GPIO_DT_FROM_NODELABEL(fp_sensor_sel);

	gpio_emul_input_set(sensor_sel_pin->port, sensor_sel_pin->pin, 0);
	zassert_equal(fpsensor_detect_get_type(), FP_SENSOR_TYPE_ELAN);
	zassert_equal(FpDetectedHardware::sensor_type(), FP_SENSOR_TYPE_ELAN);
}

ZTEST_USER(fpsensor_shim, test_shim_sensor_type_fpc)
{
	const gpio_dt_spec *sensor_sel_pin =
		GPIO_DT_FROM_NODELABEL(fp_sensor_sel);

	gpio_emul_input_set(sensor_sel_pin->port, sensor_sel_pin->pin, 1);
	zassert_equal(fpsensor_detect_get_type(), FP_SENSOR_TYPE_FPC);
	zassert_equal(FpDetectedHardware::sensor_type(), FP_SENSOR_TYPE_FPC);
}

ZTEST_USER(fpsensor_shim, test_shim_init_success)
{
	zassert_ok(fp_sensor_init());
}

ZTEST_USER(fpsensor_shim, test_shim_init_sensor_init_failed)
{
	update_sim_state([](auto &state) { state.init_result = -EINVAL; });

	zassert_equal(fp_sensor_init(), -EINVAL);
}

ZTEST_USER(fpsensor_shim, test_shim_init_algorithm_init_failed)
{
	mock_alg_init_fake.return_val = -EINVAL;

	zassert_equal(fp_sensor_init(), -EINVAL);
	zassert_equal(mock_alg_init_fake.call_count, 1);
}

ZTEST_USER(fpsensor_shim, test_shim_init_sensor_config_failed)
{
	update_sim_state([](auto &state) { state.config_result = -EINVAL; });

	zassert_equal(fp_sensor_init(), -EINVAL);
}

ZTEST_USER(fpsensor_shim, test_shim_deinit_success)
{
	zassert_ok(fp_sensor_init());
	zassert_ok(fp_sensor_deinit());
}

ZTEST_USER(fpsensor_shim, test_shim_deinit_algorithm_exit_failed)
{
	zassert_ok(fp_sensor_init());

	mock_alg_exit_fake.return_val = -EINVAL;
	zassert_equal(fp_sensor_deinit(), -EINVAL);
	zassert_equal(mock_alg_exit_fake.call_count, 1);
}

ZTEST_USER(fpsensor_shim, test_shim_deinit_sensor_deinit_failed)
{
	zassert_ok(fp_sensor_init());

	update_sim_state([](auto &state) { state.deinit_result = -EINVAL; });

	zassert_equal(fp_sensor_deinit(), -EINVAL);
}

ZTEST_USER(fpsensor_shim, test_shim_get_info_success)
{
	/* We need to initialize driver first to initialize 'error' field */
	zassert_ok(fp_sensor_init());
	zassert_ok(fp_sensor_get_info(&test_info_buffer.header,
				      sizeof(test_info_buffer)));

	zassert_equal(test_info_buffer.header.sensor_info.vendor_id,
		      FOURCC('C', 'r', 'O', 'S'));
	zassert_equal(test_info_buffer.header.sensor_info.product_id, 0);
	/*
	 * Last 4 bits of hardware id is a year of sensor production,
	 * could differ between sensors.
	 */
	zassert_equal(test_info_buffer.header.sensor_info.model_id, 0);
	zassert_equal(test_info_buffer.header.sensor_info.version, 0);
	zassert_equal(test_info_buffer.header.sensor_info.errors,
		      FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN);

	zassert_mem_equal(test_info_buffer.params.data(),
			  kDefaultSimImageFrameParams.data(),
			  sizeof(kDefaultSimImageFrameParams));
}

ZTEST_USER(fpsensor_shim, test_shim_get_info_failed)
{
	update_sim_state([](auto &state) { state.get_info_result = -EINVAL; });

	zassert_equal(fp_sensor_get_info(&test_info_buffer.header,
					 sizeof(test_info_buffer)),
		      -EINVAL);
}

ZTEST_USER(fpsensor_shim, test_shim_finger_status_present)
{
	update_sim_state([](auto &state) {
		state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	});

	zassert_equal(fp_finger_status(), FINGER_PRESENT);
}

ZTEST_USER(fpsensor_shim, test_shim_finger_status_error)
{
	update_sim_state([](auto &state) {
		state.finger_state =
			static_cast<fingerprint_finger_state>(-EINVAL);
	});

	zassert_equal(fp_finger_status(), FINGER_NONE);
}

ZTEST_USER(fpsensor_shim, test_shim_acquire_image_small_buffer_size)
{
	assert_acquire_image_with_size_modifier(-1, -EINVAL);
}

ZTEST_USER(fpsensor_shim, test_shim_acquire_image_exact_buffer_size)
{
	assert_acquire_image_with_size_modifier(0,
						FINGERPRINT_SENSOR_SCAN_GOOD);
}

ZTEST_USER(fpsensor_shim, test_shim_acquire_image_large_buffer_size)
{
	assert_acquire_image_with_size_modifier(1,
						FINGERPRINT_SENSOR_SCAN_GOOD);
}

ZTEST_SUITE(fpsensor_shim, nullptr, nullptr, fpsensor_before, nullptr, nullptr);
