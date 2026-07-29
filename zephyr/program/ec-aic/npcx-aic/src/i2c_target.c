/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2c_target_init, LOG_LEVEL_INF);

static int i2c_target_device_init(const struct device *dev)
{
	int ret;

	if (!device_is_ready(dev)) {
		LOG_ERR("i2c target device %s not ready", dev->name);
		return -ENODEV;
	}

	ret = i2c_target_driver_register(dev);
	if (ret < 0) {
		LOG_ERR("i2c target device %s register failed: %d", dev->name,
			ret);
		return ret;
	}

	LOG_INF("i2c target device %s registered successfully", dev->name);
	return 0;
}

static int i2c_target_init(void)
{
	int ret;

	ret = i2c_target_device_init(DEVICE_DT_GET(DT_NODELABEL(eeprom0)));
	if (ret < 0) {
		return ret;
	}

	return 0;
}

SYS_INIT(i2c_target_init, APPLICATION, 99);
