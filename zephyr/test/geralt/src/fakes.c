/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "fakes.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>

DEFINE_FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
DEFINE_FAKE_VALUE_FUNC(int, tc_is_attached_src, int);
DEFINE_FAKE_VALUE_FUNC(int, pinctrl_configure_pins, const pinctrl_soc_pin_t *,
		       uint8_t, uintptr_t);

static int fake_target_register(const struct device *dev)
{
	return 0;
}
static int fake_target_unregister(const struct device *dev)
{
	return 0;
}
static const struct i2c_target_driver_api fake_target_api = {
	.driver_register = fake_target_register,
	.driver_unregister = fake_target_unregister,
};
DEVICE_DT_DEFINE(DT_NODELABEL(hid_i2c_target), NULL, NULL, NULL, NULL,
		 POST_KERNEL, 90, &fake_target_api);
