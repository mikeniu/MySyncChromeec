/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <fingerprint/v4l2_types.h>
#include <fingerprint_rts5817maa.h>
#include <fingerprint_rts5817maa_private.h>

#define TEST_SENSOR_HWID 0x11

DEFINE_FFF_GLOBALS;

/* Mocks for the private RTS5817MAA sensor library. */
FAKE_VALUE_FUNC(rts_fp_return_t, rts5817maa_sensor_init);
FAKE_VALUE_FUNC(rts_fp_return_t, rts5817maa_sensor_get_hwid, uint8_t *);
FAKE_VALUE_FUNC(rts_fp_return_t, rts5817maa_sensor_power_down);
FAKE_VALUE_FUNC(rts_fp_return_t, rts5817maa_sensor_acquire_image,
		       uint8_t *, rts_capture_mode_t);
FAKE_VALUE_FUNC(rts_fp_return_t, rts5817maa_sensor_configure_detect,
		       rts_detect_mode_t);
FAKE_VALUE_FUNC(rts_finger_sts_t, rts5817maa_sensor_finger_status);

/* Converts capture types from the ec domain to the rts domain. */
rts_capture_mode_t convert_fp_capture_type_to_rts_capture_type(
	enum fingerprint_capture_type capture_type);

struct rts5817maa_fixture {
	const struct device *dev;
};

static int callback_count;

static void test_fingerprint_callback(const struct device *dev)
{
	callback_count++;
}

static rts_fp_return_t fake_get_hwid(uint8_t *id)
{
	if (id != NULL) {
		*id = TEST_SENSOR_HWID;
	}

	return RTS_FP_SUCCESS;
}

static rts_fp_return_t fake_get_hwid_fail(uint8_t *id)
{
	ARG_UNUSED(id);

	return RTS_ERROR_IO_EXCEPTION;
}

static void rts5817maa_set_irq_pin(const struct device *dev, int val)
{
	const struct rts5817maa_cfg *cfg = dev->config;

	gpio_emul_input_set(cfg->interrupt.port, cfg->interrupt.pin, val);
}

static void *rts5817maa_setup(void)
{
	static struct rts5817maa_fixture fixture = {
		.dev = DEVICE_DT_GET(DT_NODELABEL(rts5817maa)),
	};

	zassert_not_null(fixture.dev);
	return &fixture;
}

static void rts5817maa_before(void *f)
{
	struct rts5817maa_fixture *fixture = f;

	FFF_RESET_HISTORY();

	RESET_FAKE(rts5817maa_sensor_init);
	RESET_FAKE(rts5817maa_sensor_get_hwid);
	RESET_FAKE(rts5817maa_sensor_power_down);
	RESET_FAKE(rts5817maa_sensor_acquire_image);
	RESET_FAKE(rts5817maa_sensor_configure_detect);
	RESET_FAKE(rts5817maa_sensor_finger_status);

	rts5817maa_sensor_init_fake.return_val = RTS_FP_SUCCESS;
	rts5817maa_sensor_get_hwid_fake.custom_fake = fake_get_hwid;
	rts5817maa_sensor_power_down_fake.return_val = RTS_FP_SUCCESS;
	rts5817maa_sensor_acquire_image_fake.return_val = RTS_FP_SUCCESS;
	rts5817maa_sensor_configure_detect_fake.return_val = RTS_FP_SUCCESS;
	rts5817maa_sensor_finger_status_fake.return_val = RTS_FINGER_NONE;

	callback_count = 0;

	/* Keep the IRQ pin low so that rising edges are observable. */
	rts5817maa_set_irq_pin(fixture->dev, 0);
}

static void rts5817maa_after(void *f)
{
	struct rts5817maa_fixture *fixture = f;

	/* Reset the IRQ pin and the callback to prevent state leakage. */
	rts5817maa_set_irq_pin(fixture->dev, 0);
	fingerprint_config(fixture->dev, NULL);
}

#define RTS5817MAA_IMAGE_FRAME_PARAM_INITIALIZER(idx, node_id)         \
	{                                                          \
		.frame_size =                                      \
			FINGERPRINT_SENSOR_FRAME_SIZE(idx, node_id), \
		.image_data_offset_bytes =                          \
			FINGERPRINT_SENSOR_IMAGE_OFFSET(idx, node_id), \
		.pixel_format =                                    \
			FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(idx, node_id), \
		.width = FINGERPRINT_SENSOR_RES_X(idx, node_id),   \
		.height = FINGERPRINT_SENSOR_RES_Y(idx, node_id),  \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(idx, node_id),   \
		.fp_capture_type =                                  \
			FINGERPRINT_SENSOR_CAPTURE_TYPE(idx, node_id), \
		.reserved = 0,                                      \
	}

static const struct fingerprint_image_frame_params
	expected_image_frame_params_array[] = { LISTIFY(
		NUM_IMAGE_CAPTURE_TYPES, RTS5817MAA_IMAGE_FRAME_PARAM_INITIALIZER,
		(, ), DT_NODELABEL(rts5817maa)) };

ZTEST_SUITE(rts5817maa, NULL, rts5817maa_setup, rts5817maa_before,
	    rts5817maa_after, NULL);

ZTEST_F(rts5817maa, test_init_success)
{
	zassert_ok(fingerprint_init(fixture->dev));
	zassert_equal(rts5817maa_sensor_init_fake.call_count, 1);
}

ZTEST_F(rts5817maa, test_init_error_bad_hwid)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	rts5817maa_sensor_init_fake.return_val = RTS_ERROR_NOT_SUPPORT_SENSOR;

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_true(sensor_info.errors & FINGERPRINT_ERROR_BAD_HWID);
	zassert_true(sensor_info.errors & FINGERPRINT_ERROR_INIT_FAIL);
}

ZTEST_F(rts5817maa, test_init_error_spi_comm)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	rts5817maa_sensor_init_fake.return_val = RTS_ERROR_IO_EXCEPTION;

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_true(sensor_info.errors & FINGERPRINT_ERROR_SPI_COMM);
	zassert_true(sensor_info.errors & FINGERPRINT_ERROR_INIT_FAIL);
}

ZTEST_F(rts5817maa, test_init_error_unmapped)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	rts5817maa_sensor_init_fake.return_val = RTS_ERROR_TIMEOUT;

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_true(sensor_info.errors & FINGERPRINT_ERROR_INIT_FAIL);
}

ZTEST_F(rts5817maa, test_deinit_success)
{
	zassert_ok(fingerprint_deinit(fixture->dev));
}

ZTEST_F(rts5817maa, test_get_info)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	/* We need to initialize driver first to initialize 'error' field */
	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_equal(
		num_params, NUM_IMAGE_CAPTURE_TYPES,
		"fingerprint_get_info returned an unexpected number of params");

	zassert_equal(sensor_info.vendor_id, FOURCC('R', 'T', 'S', ' '));
	zassert_equal(sensor_info.product_id, 9);
	zassert_equal(sensor_info.model_id, TEST_SENSOR_HWID);
	zassert_equal(sensor_info.version, 1);
	zassert_equal(sensor_info.errors,
		      FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN);

	for (int i = 0; i < NUM_IMAGE_CAPTURE_TYPES; ++i) {
		zassert_equal(
			memcmp(&image_frame_params_array[i],
			       &expected_image_frame_params_array[i],
			       sizeof(struct fingerprint_image_frame_params)),
			0, "Struct comparison failed at index %d", i);
	}
}

ZTEST_F(rts5817maa, test_get_info_null_args)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_equal(fingerprint_get_info(fixture->dev, NULL,
					   image_frame_params_array,
					   &num_params),
		      -EINVAL);
	zassert_equal(fingerprint_get_info(fixture->dev, &sensor_info, NULL,
					   &num_params),
		      -EINVAL);
	zassert_equal(fingerprint_get_info(fixture->dev, &sensor_info,
					   image_frame_params_array, NULL),
		      -EINVAL);
}

ZTEST_F(rts5817maa, test_get_info_small_capacity)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = 0;

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_equal(fingerprint_get_info(fixture->dev, &sensor_info,
					   image_frame_params_array,
					   &num_params),
		      -EINVAL);
}

ZTEST_F(rts5817maa, test_get_info_hwid_error)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	zassert_ok(fingerprint_init(fixture->dev));
	rts5817maa_sensor_get_hwid_fake.custom_fake = fake_get_hwid_fail;
	zassert_equal(fingerprint_get_info(fixture->dev, &sensor_info,
					   image_frame_params_array,
					   &num_params),
		      -EINVAL);
}

ZTEST_F(rts5817maa, test_config)
{
	zassert_ok(fingerprint_config(fixture->dev, test_fingerprint_callback));
}

ZTEST_F(rts5817maa, test_irq_invokes_callback)
{
	zassert_ok(fingerprint_config(fixture->dev, test_fingerprint_callback));
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_DETECT));

	rts5817maa_set_irq_pin(fixture->dev, 1);
	zassert_equal(callback_count, 1);
}

ZTEST_F(rts5817maa, test_set_mode_detect)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_DETECT));
	zassert_equal(rts5817maa_sensor_configure_detect_fake.call_count, 1);
	zassert_equal(rts5817maa_sensor_configure_detect_fake.arg0_val,
		      RTS_DETECT_MODE_NORMAL);
}

ZTEST_F(rts5817maa, test_set_mode_low_power)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_LOW_POWER));
	zassert_equal(rts5817maa_sensor_power_down_fake.call_count, 1);
}

ZTEST_F(rts5817maa, test_set_mode_idle)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_IDLE));
}

ZTEST_F(rts5817maa, test_set_mode_invalid)
{
	zassert_equal(fingerprint_set_mode(fixture->dev, UINT16_MAX),
		      -ENOTSUP);
}

ZTEST_F(rts5817maa, test_maintenance)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_maintenance(fixture->dev, buffer,
					      sizeof(buffer)),
		      0);
}

ZTEST_F(rts5817maa, test_finger_status_present)
{
	rts5817maa_sensor_finger_status_fake.return_val = RTS_FINGER_PRESENT;
	zassert_equal(fingerprint_finger_status(fixture->dev),
		      FINGERPRINT_FINGER_STATE_PRESENT);
}

ZTEST_F(rts5817maa, test_finger_status_none)
{
	rts5817maa_sensor_finger_status_fake.return_val = RTS_FINGER_NONE;
	zassert_equal(fingerprint_finger_status(fixture->dev),
		      FINGERPRINT_FINGER_STATE_NONE);
}

ZTEST_F(rts5817maa, test_finger_status_unmapped)
{
	rts5817maa_sensor_finger_status_fake.return_val = (rts_finger_sts_t)42;
	zassert_equal(fingerprint_finger_status(fixture->dev),
		      FINGERPRINT_FINGER_STATE_NONE);
}

ZTEST_F(rts5817maa, test_acquire_image_success)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_ok(fingerprint_acquire_image(
		fixture->dev, FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE, buffer,
		sizeof(buffer)));
	zassert_equal(rts5817maa_sensor_acquire_image_fake.call_count, 1);
	zassert_equal(rts5817maa_sensor_acquire_image_fake.arg1_val,
		      RTS_CAPTURE_NORMAL_FORMAT);
}

ZTEST_F(rts5817maa, test_acquire_image_fast_finger_up)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	rts5817maa_sensor_acquire_image_fake.return_val =
		RTS_ERROR_FAST_FINGER_UP;
	zassert_equal(fingerprint_acquire_image(fixture->dev,
						FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT,
						buffer, sizeof(buffer)),
		      FINGERPRINT_SENSOR_SCAN_TOO_FAST);
}

ZTEST_F(rts5817maa, test_acquire_image_partial)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	rts5817maa_sensor_acquire_image_fake.return_val =
		RTS_ERROR_IMAGE_PARTIAL;
	zassert_equal(fingerprint_acquire_image(fixture->dev,
						FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT,
						buffer, sizeof(buffer)),
		      FINGERPRINT_SENSOR_SCAN_LOW_SENSOR_COVERAGE);
}

ZTEST_F(rts5817maa, test_acquire_image_unmapped_error)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	rts5817maa_sensor_acquire_image_fake.return_val = RTS_ERROR_TIMEOUT;
	zassert_equal(fingerprint_acquire_image(fixture->dev,
						FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT,
						buffer, sizeof(buffer)),
		      RTS_ERROR_TIMEOUT);
}

ZTEST_F(rts5817maa, test_acquire_image_invalid_type)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_acquire_image(fixture->dev,
						FINGERPRINT_CAPTURE_TYPE_PATTERN0,
						buffer, sizeof(buffer)),
		      -EINVAL);
	zassert_equal(rts5817maa_sensor_acquire_image_fake.call_count, 0);
}

ZTEST_F(rts5817maa, test_acquire_image_small_buffer)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_acquire_image(
			      fixture->dev,
			      FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE, buffer,
			      sizeof(buffer) - 1),
		      -EINVAL);
	zassert_equal(rts5817maa_sensor_acquire_image_fake.call_count, 0);
}

ZTEST_F(rts5817maa, test_convert_capture_type)
{
	zassert_equal(
		convert_fp_capture_type_to_rts_capture_type(
			FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT),
		RTS_CAPTURE_NORMAL_FORMAT);
	zassert_equal(
		convert_fp_capture_type_to_rts_capture_type(
			FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE),
		RTS_CAPTURE_NORMAL_FORMAT);
	zassert_equal(
		convert_fp_capture_type_to_rts_capture_type(
			FINGERPRINT_CAPTURE_TYPE_PATTERN0),
		RTS_CAPTURE_TYPE_INVALID);
	zassert_equal(
		convert_fp_capture_type_to_rts_capture_type(
			FINGERPRINT_CAPTURE_TYPE_PATTERN1),
		RTS_CAPTURE_TYPE_INVALID);
	zassert_equal(
		convert_fp_capture_type_to_rts_capture_type(
			FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST),
		RTS_CAPTURE_TYPE_INVALID);
	zassert_equal(
		convert_fp_capture_type_to_rts_capture_type(
			FINGERPRINT_CAPTURE_TYPE_DEFECT_PXL_TEST),
		RTS_CAPTURE_TYPE_INVALID);
	zassert_equal(
		convert_fp_capture_type_to_rts_capture_type(
			FINGERPRINT_CAPTURE_TYPE_ABNORMAL_TEST),
		RTS_CAPTURE_TYPE_INVALID);
	zassert_equal(
		convert_fp_capture_type_to_rts_capture_type(
			FINGERPRINT_CAPTURE_TYPE_NOISE_TEST),
		RTS_CAPTURE_TYPE_INVALID);
	zassert_equal(
		convert_fp_capture_type_to_rts_capture_type(
			FINGERPRINT_CAPTURE_TYPE_RESET_TEST),
		RTS_CAPTURE_TYPE_INVALID);
	zassert_equal(
		convert_fp_capture_type_to_rts_capture_type(
			FINGERPRINT_CAPTURE_TYPE_MAX),
		RTS_CAPTURE_TYPE_INVALID);
}
