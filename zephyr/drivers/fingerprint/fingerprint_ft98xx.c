/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ft_ft98xx

#include "fingerprint_ft98xx.h"
#include "fingerprint_ft98xx_pal.h"
#include "fingerprint_ft98xx_private.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <drivers/fingerprint.h>
#include <fingerprint/v4l2_types.h>

LOG_MODULE_REGISTER(cros_fingerprint, LOG_LEVEL_INF);

static inline int ft98xx_enable_irq(const struct device *dev)
{
	const struct ft98xx_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					     GPIO_INT_EDGE_TO_ACTIVE);
	if (rc < 0) {
		LOG_ERR("Can't enable interrupt: %d", rc);
	}

	return rc;
}

static inline int ft98xx_disable_irq(const struct device *dev)
{
	const struct ft98xx_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt, GPIO_INT_DISABLE);
	if (rc < 0) {
		LOG_ERR("Can't disable interrupt: %d", rc);
	}

	return rc;
}

static int ft98xx_set_mode(const struct device *dev,
			   enum fingerprint_sensor_mode mode)
{
	int rc = 0;

	switch (mode) {
	case FINGERPRINT_SENSOR_MODE_DETECT:
		if (IS_ENABLED(CONFIG_HAVE_FT98XX_PRIVATE_DRIVER)) {
			rc = ft_sensor_set_mode(FOCAL_SENSOR_MODE_DETECT);
			if (rc == 0) {
				rc = ft98xx_enable_irq(dev);
			}
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_LOW_POWER:
		if (IS_ENABLED(CONFIG_HAVE_FT98XX_PRIVATE_DRIVER)) {
			rc = ft_sensor_set_mode(FOCAL_SENSOR_MODE_LOW_POWER);
			if (rc == 0) {
				rc = ft98xx_disable_irq(dev);
			}
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_IDLE:
		rc = ft98xx_disable_irq(dev);
		break;

	default:
		rc = -ENOTSUP;
	}

	return rc;
}

static int ft98xx_init(const struct device *dev)
{
	int rc = 0;
	struct ft98xx_data *data = dev->data;
	data->errors = FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN;
	data->irq_event = 0;

	if (!IS_ENABLED(CONFIG_HAVE_FT98XX_PRIVATE_DRIVER)) {
		return 0;
	}

	sensor_param_t sensor_param = { 0 };
	sensor_param.hw_rst_func_impl = ft_sensor_hw_reset;
	sensor_param.spi_write_func_impl = ft_spi_write;
	sensor_param.spi_write_read_func_impl = ft_spi_write_then_read;
	sensor_param.delay_ms_func_impl = ft_delay_ms;
	rc = ft_sensor_init(sensor_param);

	if (rc == 0) {
		uint16_t chipid = ft_sensor_query_chipid();
		uint16_t cols = ft_sensor_query_cols();
		uint16_t rows = ft_sensor_query_rows();
		LOG_INF("sensor id: %x, cols:%d, rows:%d", chipid, cols, rows);
	} else {
		LOG_ERR("ft98xx sensor init fail, result:%d", rc);
		return rc;
	}

	return 0;
}

static int ft98xx_deinit(const struct device *dev)
{
	return 0;
}

static int ft98xx_get_info(
	const struct device *dev, struct fingerprint_sensor_info *sensor_info,
	struct fingerprint_image_frame_params image_frame_params_array[],
	uint8_t *num_params)
{
	const struct ft98xx_cfg *cfg = dev->config;
	struct ft98xx_data *data = dev->data;

	if ((sensor_info == NULL) || (num_params == NULL) ||
	    (image_frame_params_array == NULL)) {
		return -EINVAL;
	}

	uint8_t capacity = *num_params;
	uint8_t num_defined_configs = cfg->sensor_info.num_capture_types;

	if (capacity < num_defined_configs) {
		return -EINVAL;
	}

	memcpy(sensor_info, &cfg->sensor_info,
	       sizeof(struct fingerprint_sensor_info));

	memcpy(image_frame_params_array, cfg->sensor_image_configs,
	       num_defined_configs *
		       sizeof(struct fingerprint_image_frame_params));

	*num_params = num_defined_configs;

	if (IS_ENABLED(CONFIG_HAVE_FT98XX_PRIVATE_DRIVER))
		sensor_info->model_id = ft_sensor_query_chipid();

	sensor_info->errors = data->errors;

	if ((data->irq_event == 0) &&
	    (IS_ENABLED(CONFIG_HAVE_FT98XX_PRIVATE_DRIVER)))
		sensor_info->errors |= FINGERPRINT_ERROR_NO_IRQ;

	return 0;
}

static int ft98xx_config(const struct device *dev, fingerprint_callback_t cb)
{
	struct ft98xx_data *data = dev->data;

	data->callback = cb;

	ft98xx_enable_irq(dev);

	return 0;
}

static int ft98xx_maintenance(const struct device *dev, uint8_t *buf,
			      size_t size)
{
	return 0;
}

static int ft98xx_get_frame_size(const struct device *dev,
				 enum fingerprint_capture_type capture_type)
{
	const struct ft98xx_cfg *cfg = dev->config;

	for (int i = 0; i < NUM_IMAGE_CAPTURE_TYPES; i++) {
		if (cfg->sensor_image_configs[i].fp_capture_type ==
		    capture_type)
			return cfg->sensor_image_configs[i].frame_size;
	}

	/*unsupported capture_type, return error*/
	return -EINVAL;
}

static int ft98xx_acquire_image(const struct device *dev,
				enum fingerprint_capture_type capture_type,
				uint8_t *image_buf, size_t image_buf_size)
{
	int ret = -1;
	int frame_size = ft98xx_get_frame_size(dev, capture_type);
	if ((frame_size < 0) || (image_buf_size < frame_size))
		return -EINVAL;

	if (!IS_ENABLED(CONFIG_HAVE_FT98XX_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	memset(image_buf, 0, image_buf_size);
	ret = ft_sensor_acquire_image_with_mode(image_buf, capture_type);

	return ret;
}

static int ft98xx_finger_status(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_HAVE_FT98XX_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	if (ft_sensor_query_finger_status_simple() == 1) {
		LOG_DBG("FINGER_PRESENT");
		return FINGERPRINT_FINGER_STATE_PRESENT;
	} else {
		LOG_DBG("FINGER_NONE");
		return FINGERPRINT_FINGER_STATE_NONE;
	}
}

static DEVICE_API(fingerprint, cros_fp_ft98xx_driver_api) = {
	.init = ft98xx_init,
	.deinit = ft98xx_deinit,
	.config = ft98xx_config,
	.get_info = ft98xx_get_info,
	.maintenance = ft98xx_maintenance,
	.set_mode = ft98xx_set_mode,
	.acquire_image = ft98xx_acquire_image,
	.finger_status = ft98xx_finger_status,
};

static void ft98xx_irq(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	struct ft98xx_data *data = CONTAINER_OF(cb, struct ft98xx_data, irq_cb);

	ft98xx_disable_irq(data->dev);

	data->irq_event = 1;

	if (data->callback != NULL) {
		data->callback(dev);
	}
}

static int ft98xx_init_driver(const struct device *dev)
{
	const struct ft98xx_cfg *cfg = dev->config;
	struct ft98xx_data *data = dev->data;
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
	gpio_init_callback(&data->irq_cb, ft98xx_irq, BIT(cfg->interrupt.pin));
	gpio_add_callback_dt(&cfg->interrupt, &data->irq_cb);

	return 0;
}

#define FT98XX_SENSOR_INFO(inst)                                           \
	{                                                                  \
		.vendor_id = FOURCC('F', 'T', ' ', ' '),                   \
		.product_id = 9,                                           \
		.model_id = 1,                                             \
		.version = 1,                                              \
		.num_capture_types =                                       \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)), \
	}

#define FT98XX_IMAGE_PARAM_INITIALIZER(idx, inst)                              \
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

#define FT98XX_BUILD_ASSERT_IMAGE_SIZE(idx, inst)                              \
	BUILD_ASSERT(                                                          \
		CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE >=                        \
			FINGERPRINT_SENSOR_FRAME_SIZE(idx, DT_DRV_INST(inst)), \
		"FP image buffer size smaller than raw image size at index " #idx);

#define FT98XX_DEFINE(inst)                                                         \
	static struct ft98xx_data ft98xx_data_##inst;                               \
	static const struct ft98xx_cfg ft98xx_cfg_##inst = {                        \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER |              \
							  SPI_WORD_SET(8)),         \
		.interrupt = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),                \
		.reset_pin = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),              \
		.sensor_info = FT98XX_SENSOR_INFO(inst),                            \
		.sensor_image_configs = { LISTIFY(                                  \
			FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),          \
			FT98XX_IMAGE_PARAM_INITIALIZER, (, ), inst) },              \
	};                                                                          \
	IF_ENABLED(CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE,                            \
		   LISTIFY(FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)),       \
			   FT98XX_BUILD_ASSERT_IMAGE_SIZE, (;), inst))              \
	BUILD_ASSERT(                                                               \
		FINGERPRINT_SENSOR_NUM_CONFIGS(DT_DRV_INST(inst)) <=                \
			NUM_IMAGE_CAPTURE_TYPES,                                    \
		"FT98XX: Number of image configs exceeds NUM_IMAGE_CAPTURE_TYPES"); \
	DEVICE_DT_INST_DEFINE(inst, ft98xx_init_driver, NULL,                       \
			      &ft98xx_data_##inst, &ft98xx_cfg_##inst,              \
			      POST_KERNEL,                                          \
			      CONFIG_FINGERPRINT_SENSOR_INIT_PRIORITY,              \
			      &cros_fp_ft98xx_driver_api)

DT_INST_FOREACH_STATUS_OKAY(FT98XX_DEFINE);
