/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_FT98XX_H_
#define PLATFORM_EC_ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_FT98XX_H_

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#include <drivers/fingerprint.h>

struct ft98xx_cfg {
	struct spi_dt_spec spi;
	struct gpio_dt_spec interrupt;
	struct gpio_dt_spec reset_pin;
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params sensor_image_configs[];
};

struct ft98xx_data {
	const struct device *dev;
	fingerprint_callback_t callback;
	struct gpio_callback irq_cb;
	uint16_t errors;
};

#endif /* PLATFORM_EC_ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_FT98XX_H_ */
