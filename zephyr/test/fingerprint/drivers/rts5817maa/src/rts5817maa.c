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
#define RTS5817MAA_EXPECTED_PRODUCT_ID 9
#define RTS5817MAA_EXPECTED_VERSION 1

DEFINE_FFF_GLOBALS;

/* Mocks for the private RTS5817MAA sensor library. */
FAKE_VALUE_FUNC(rts_fp_return_t, rts5817maa_sensor_init);
FAKE_VALUE_FUNC(rts_fp_return_t, rts5817maa_sensor_get_hwid, uint8_t *);
FAKE_VALUE_FUNC(rts_fp_return_t, rts5817maa_sensor_power_down);
FAKE_VALUE_FUNC(rts_fp_return_t, rts5817maa_sensor_configure_detect,
		       rts_detect_mode_t);

struct rts5817maa_fixture {
	const struct device *dev;
};

static int callback_count;
static const struct device *callback_dev;

static void test_fingerprint_callback(const struct device *dev)
{
	callback_count++;
	callback_dev = dev;
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
	RESET_FAKE(rts5817maa_sensor_configure_detect);

	rts5817maa_sensor_init_fake.return_val = RTS_FP_SUCCESS;
	rts5817maa_sensor_get_hwid_fake.custom_fake = fake_get_hwid;
	rts5817maa_sensor_power_down_fake.return_val = RTS_FP_SUCCESS;
	rts5817maa_sensor_configure_detect_fake.return_val = RTS_FP_SUCCESS;

	callback_count = 0;
	callback_dev = NULL;

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

	zassert_equal(fingerprint_init(fixture->dev), -EIO);
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_equal(sensor_info.errors,
		      FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN |
			      FINGERPRINT_ERROR_BAD_HWID |
			      FINGERPRINT_ERROR_INIT_FAIL);
}

ZTEST_F(rts5817maa, test_init_error_spi_comm)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	rts5817maa_sensor_init_fake.return_val = RTS_ERROR_IO_EXCEPTION;

	zassert_equal(fingerprint_init(fixture->dev), -EIO);
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_equal(sensor_info.errors,
		      FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN |
			      FINGERPRINT_ERROR_SPI_COMM |
			      FINGERPRINT_ERROR_INIT_FAIL);
}

ZTEST_F(rts5817maa, test_init_error_unmapped)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	rts5817maa_sensor_init_fake.return_val = RTS_ERROR_TIMEOUT;

	zassert_equal(fingerprint_init(fixture->dev), -EIO);
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_equal(sensor_info.errors,
		      FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN |
			      FINGERPRINT_ERROR_INIT_FAIL);
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
	zassert_equal(sensor_info.product_id,
		      RTS5817MAA_EXPECTED_PRODUCT_ID);
	zassert_equal(sensor_info.model_id, TEST_SENSOR_HWID);
	zassert_equal(sensor_info.version, RTS5817MAA_EXPECTED_VERSION);
	zassert_equal(sensor_info.errors,
		      FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN);

	zassert_mem_equal(image_frame_params_array,
			  expected_image_frame_params_array,
			  sizeof(expected_image_frame_params_array),
			  "Image frame params array mismatch");
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
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES - 1;

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_equal(fingerprint_get_info(fixture->dev, &sensor_info,
					   image_frame_params_array,
					   &num_params),
		      -EINVAL);
}

ZTEST_F(rts5817maa, test_get_info_large_capacity)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES + 1];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES + 1;

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_equal(num_params, NUM_IMAGE_CAPTURE_TYPES,
		      "fingerprint_get_info did not update num_params to actual count");
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

ZTEST_F(rts5817maa, test_config_null_callback)
{
	zassert_ok(fingerprint_config(fixture->dev, NULL));
}

ZTEST_F(rts5817maa, test_irq_invokes_callback)
{
	zassert_ok(fingerprint_config(fixture->dev, test_fingerprint_callback));
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_DETECT));

	rts5817maa_set_irq_pin(fixture->dev, 1);
	zassert_equal(callback_count, 1);
	zassert_equal(callback_dev, fixture->dev,
		      "callback must receive the fingerprint sensor device");
}

ZTEST_F(rts5817maa, test_irq_ignored_when_not_detecting)
{
	zassert_ok(fingerprint_config(fixture->dev, test_fingerprint_callback));
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_IDLE));

	rts5817maa_set_irq_pin(fixture->dev, 1);
	zassert_equal(callback_count, 0);
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

ZTEST_F(rts5817maa, test_set_mode_detect_error)
{
	rts5817maa_sensor_configure_detect_fake.return_val =
		RTS_ERROR_IO_EXCEPTION;

	zassert_ok(fingerprint_config(fixture->dev, test_fingerprint_callback));
	zassert_equal(fingerprint_set_mode(fixture->dev,
					   FINGERPRINT_SENSOR_MODE_DETECT),
		      -EIO);
	zassert_equal(rts5817maa_sensor_configure_detect_fake.call_count, 1);

	rts5817maa_set_irq_pin(fixture->dev, 1);
	zassert_equal(callback_count, 0,
		      "IRQ must be disabled after detect configuration fails");
}

ZTEST_F(rts5817maa, test_set_mode_low_power_error)
{
	rts5817maa_sensor_power_down_fake.return_val = RTS_ERROR_IO_EXCEPTION;

	zassert_equal(fingerprint_set_mode(fixture->dev,
					   FINGERPRINT_SENSOR_MODE_LOW_POWER),
		      -EIO);
	zassert_equal(rts5817maa_sensor_power_down_fake.call_count, 1);
}

ZTEST_F(rts5817maa, test_set_mode_idle)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_IDLE));
}

ZTEST_F(rts5817maa, test_set_mode_invalid)
{
	zassert_equal(fingerprint_set_mode(fixture->dev,
					   FINGERPRINT_SENSOR_MODE_DETECT + 1),
		      -ENOTSUP);
	zassert_equal(fingerprint_set_mode(fixture->dev, UINT16_MAX),
		      -ENOTSUP);
}

ZTEST_F(rts5817maa, test_maintenance)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE] = { 0 };

	zassert_equal(fingerprint_maintenance(fixture->dev, buffer,
					      sizeof(buffer)),
		      0);
}
