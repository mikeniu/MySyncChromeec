/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT realtek_rts5817maa

#include "fingerprint_rts5817maa.h"
#include "fingerprint_rts5817maa_pal.h"
#include "fingerprint_rts5817maa_private.h"

#include <assert.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>

#include <drivers/fingerprint.h>
#include <fingerprint/v4l2_types.h>

LOG_MODULE_REGISTER(cros_fingerprint, LOG_LEVEL_INF);

IF_DISABLED(CONFIG_ZTEST, (static))
rts_capture_mode_t convert_fp_capture_type_to_rts_capture_type(
 	enum fingerprint_capture_type capture_type)
{
	switch (capture_type) {
	case FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT:
	case FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE:
		return RTS_CAPTURE_NORMAL_FORMAT;
	case FINGERPRINT_CAPTURE_TYPE_PATTERN0:
	case FINGERPRINT_CAPTURE_TYPE_PATTERN1:
	case FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST:
	case FINGERPRINT_CAPTURE_TYPE_DEFECT_PXL_TEST:
	case FINGERPRINT_CAPTURE_TYPE_ABNORMAL_TEST:
	case FINGERPRINT_CAPTURE_TYPE_NOISE_TEST:
	case FINGERPRINT_CAPTURE_TYPE_RESET_TEST:
	default:
		return RTS_CAPTURE_TYPE_INVALID;
	}
}

int convert_rts_get_image_error_code(rts_fp_return_t code)
{
	switch (code) {
	case RTS_ERROR_FAST_FINGER_UP:
		return FINGERPRINT_SENSOR_SCAN_TOO_FAST;
	case RTS_ERROR_IMAGE_PARTIAL:
		return FINGERPRINT_SENSOR_SCAN_LOW_SENSOR_COVERAGE;
	default:
		return code;
	}
}

int convert_rts_snr_init_error_code(rts_fp_return_t code)
{
	switch (code) {
	case RTS_ERROR_NOT_SUPPORT_SENSOR:
		return FINGERPRINT_ERROR_BAD_HWID;
	case RTS_ERROR_IO_EXCEPTION:
		return FINGERPRINT_ERROR_SPI_COMM;
	default:
		return 0;
	}
}

static inline int rts5817maa_enable_irq(const struct device *dev)
{
	const struct rts5817maa_cfg *cfg = dev->config;
	int ret;

	ret = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					      GPIO_INT_EDGE_RISING);
	if (ret < 0) {
		LOG_ERR("Can't enable interrupt: %d", ret);
	}

	return ret;
}

static inline int rts5817maa_disable_irq(const struct device *dev)
{
	const struct rts5817maa_cfg *cfg = dev->config;
	int ret;

	ret = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					      GPIO_INT_DISABLE);
	if (ret < 0) {
		LOG_ERR("Can't disable interrupt: %d", ret);
	}

	return ret;
}

static int rts5817maa_init(const struct device *dev)
{
	struct rts5817maa_data *data = dev->data;
	int ret;

	data->errors = FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN;

	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	ret = convert_rts_snr_init_error_code(rts5817maa_sensor_init());

	if (ret) {
		LOG_ERR("sensor init failed %d", ret);
		data->errors |= (ret | FINGERPRINT_ERROR_INIT_FAIL);
	}

	return 0;
}

static int rts5817maa_deinit(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	rts5817maa_disable_irq(dev);

	return 0;
}

static int rts5817maa_config(const struct device *dev, fingerprint_callback_t cb)
{
	struct rts5817maa_data *data = dev->data;

	data->callback = cb;

	return 0;
}

static int rts5817maa_get_info(
	const struct device *dev, struct fingerprint_sensor_info *sensor_info,
	struct fingerprint_image_frame_params image_frame_params_array[],
	uint8_t *num_params)
{
	const struct rts5817maa_cfg *cfg = dev->config;
	struct rts5817maa_data *data = dev->data;
	uint8_t sensor_id = 0;
	int ret = 0;

	if (sensor_info == NULL || num_params == NULL ||
	    image_frame_params_array == NULL) {
		return -EINVAL;
	}

	uint8_t capacity = *num_params;
	uint8_t num_defined_configs = cfg->sensor_info.num_capture_types;

	if (capacity < num_defined_configs) {
		return -EINVAL;
	}

	BUILD_ASSERT(sizeof(cfg->sensor_info) == sizeof(*sensor_info),
		     "struct fingerprint_sensor_info size mismatch");

	memcpy(sensor_info, &cfg->sensor_info,
	       sizeof(struct fingerprint_sensor_info));

	memcpy(image_frame_params_array, cfg->sensor_image_configs,
	       num_defined_configs *
		       sizeof(struct fingerprint_image_frame_params));

	*num_params = num_defined_configs;

	if (IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_DRIVER)) {
		ret = rts5817maa_sensor_get_hwid(&sensor_id);
	}

	if (ret) {
		LOG_ERR("Failed to get sensor hwid: %d", ret);
		return -EINVAL;
	}

	sensor_info->model_id = sensor_id;
	sensor_info->errors = data->errors;

	return 0;
}

static int rts5817maa_maintenance(const struct device *dev, uint8_t *buf,
			      size_t size)
{
	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	return 0;
}

static int rts5817maa_set_mode(const struct device *dev,
			   enum fingerprint_sensor_mode mode)
{
	int ret = 0;

	switch (mode) {
	case FINGERPRINT_SENSOR_MODE_DETECT:
		if (IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_DRIVER)) {
			LOG_INF("Sensor changes mode to finger detect");
			ret = rts5817maa_enable_irq(dev);
			rts5817maa_sensor_configure_detect(RTS_DETECT_MODE_NORMAL);
		} else {
			ret = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_LOW_POWER:
		if (IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_DRIVER)) {
			rts5817maa_sensor_power_down();
			ret = rts5817maa_disable_irq(dev);
		} else {
			ret = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_IDLE:
		ret = rts5817maa_disable_irq(dev);
		break;

	default:
		ret = -ENOTSUP;
	}

	return ret;
}

static int rts5817maa_acquire_image(const struct device *dev,
				enum fingerprint_capture_type capture_type,
				uint8_t *image, size_t size)
{
	int ret = 0;

	if (size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE)
		return -EINVAL;

	rts_capture_mode_t rts_capture_mode =
		convert_fp_capture_type_to_rts_capture_type(capture_type);

	if (rts_capture_mode == RTS_CAPTURE_TYPE_INVALID) {
		LOG_ERR("Unsupported capture_type %d provided", capture_type);
		return -EINVAL;
	}

	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_DRIVER)) {
	 	return -ENOTSUP;
	}

	ret = convert_rts_get_image_error_code(
	rts5817maa_sensor_acquire_image(image, rts_capture_mode));
	if (ret) {
		LOG_ERR("Failed to acquire image with capture_type %d: %d",
		capture_type, ret);
	}

	return ret;
}

static int rts5817maa_finger_status(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_HAVE_RTS5817MAA_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	rts_finger_sts_t ret = rts5817maa_sensor_finger_status();

	switch (ret) {
	case RTS_FINGER_PRESENT:
		return FINGERPRINT_FINGER_STATE_PRESENT;
	case RTS_FINGER_NONE:
	default:
		return FINGERPRINT_FINGER_STATE_NONE;
	}
}

static const struct fingerprint_driver_api cros_fp_rts5817maa_driver_api = {
	.init = rts5817maa_init,
	.deinit = rts5817maa_deinit,
	.config = rts5817maa_config,
	.get_info = rts5817maa_get_info,
	.maintenance = rts5817maa_maintenance,
	.set_mode = rts5817maa_set_mode,
	.acquire_image = rts5817maa_acquire_image,
	.finger_status = rts5817maa_finger_status,
};

static void rts5817maa_irq(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	struct rts5817maa_data *data = CONTAINER_OF(cb, struct rts5817maa_data, irq_cb);

	rts5817maa_disable_irq(data->dev);

	if (data->callback != NULL) {
		data->callback(dev);
	}
}

static int rts5817maa_init_driver(const struct device *dev)
{
	const struct rts5817maa_cfg *cfg = dev->config;
	struct rts5817maa_data *data = dev->data;
	int ret;

	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR("SPI bus is not ready");
		return -EINVAL;
	}

	if (!gpio_is_ready_dt(&cfg->reset_pin)) {
		LOG_ERR("Port for sensor reset GPIO is not ready");
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&cfg->reset_pin, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Can't configure sensor reset pin");
		return ret;
	}

	if (!gpio_is_ready_dt(&cfg->interrupt)) {
		LOG_ERR("Port for interrupt GPIO is not ready");
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&cfg->interrupt, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Can't configure interrupt pin");
		return ret;
	}

	data->dev = dev;
	gpio_init_callback(&data->irq_cb, rts5817maa_irq, BIT(cfg->interrupt.pin));
	gpio_add_callback_dt(&cfg->interrupt, &data->irq_cb);

	return 0;
}

#define RTS5817MAA_SENSOR_INFO(inst)                                           \
	{                                                                  \
		.vendor_id = FOURCC('R', 'T', 'S', ' '),                   \
		.product_id = 9,                                           \
		.model_id = 1,                                             \
		.version = 1,                                              \
		.num_capture_types =                                       \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)), \
	}

#define RTS5817MAA_IMAGE_PARAM_INITIALIZER(idx, inst)                              \
	{                                                                      \
		.frame_size =                                                  \
			FINGERPRINT_SENSOR_FRAME_SIZE(idx, DT_DRV_INST(inst)), \
		.pixel_format = FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(          \
			idx, DT_DRV_INST(inst)),                               \
		.width = FINGERPRINT_SENSOR_RES_X(idx, DT_DRV_INST(inst)),     \
		.height = FINGERPRINT_SENSOR_RES_Y(idx, DT_DRV_INST(inst)),    \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(idx, DT_DRV_INST(inst)),     \
		.fp_capture_type = FINGERPRINT_SENSOR_CAPTURE_TYPE(            \
			idx, DT_DRV_INST(inst)),                               \
		.reserved = 0,                                                 \
	}

#define RTS5817MAA_BUILD_ASSERT_IMAGE_SIZE(idx, inst)                              \
	BUILD_ASSERT(                                                          \
		CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE >=                        \
			FINGERPRINT_SENSOR_FRAME_SIZE(idx, DT_DRV_INST(inst)), \
		"FP image buffer size smaller than raw image size at index " #idx);

#define RTS5817MAA_DEFINE(inst)                                                         \
	static struct rts5817maa_data rts5817maa_data_##inst;                               \
	static const struct rts5817maa_cfg rts5817maa_cfg_##inst = {                        \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER |              \
							  SPI_WORD_SET(8)),         \
		.interrupt = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),                \
		.reset_pin = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),              \
		.sensor_info = RTS5817MAA_SENSOR_INFO(inst),                            \
		.sensor_image_configs = { LISTIFY(                                  \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),          \
			RTS5817MAA_IMAGE_PARAM_INITIALIZER, (, ), inst) },              \
	};                                                                          \
	LISTIFY(FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),                  \
		RTS5817MAA_BUILD_ASSERT_IMAGE_SIZE, (;), inst)                          \
	BUILD_ASSERT(                                                               \
		FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)) <=                \
			NUM_IMAGE_CAPTURE_TYPES,                                    \
		"RTS5817MAA: Number of image configs exceeds NUM_IMAGE_CAPTURE_TYPES"); \
	DEVICE_DT_INST_DEFINE(inst, rts5817maa_init_driver, NULL,                       \
			      &rts5817maa_data_##inst, &rts5817maa_cfg_##inst,              \
			      POST_KERNEL,                                          \
			      CONFIG_FINGERPRINT_SENSOR_INIT_PRIORITY,              \
			      &cros_fp_rts5817maa_driver_api)

DT_INST_FOREACH_STATUS_OKAY(RTS5817MAA_DEFINE);
