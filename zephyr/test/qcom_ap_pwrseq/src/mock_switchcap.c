/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock_switchcap.h"

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <drivers/cros_switchcap.h>

bool mock_switchcap_enabled;
bool mock_switchcap_power_good;

int cros_switchcap_set_power(const struct device *dev, bool enable)
{
	ARG_UNUSED(dev);

	mock_switchcap_enabled = enable;
	return 0;
}

bool cros_switchcap_is_enabled(const struct device *dev)
{
	ARG_UNUSED(dev);

	return mock_switchcap_enabled;
}

bool cros_switchcap_is_power_good(const struct device *dev)
{
	ARG_UNUSED(dev);

	return mock_switchcap_power_good;
}

DEVICE_DT_DEFINE(DT_NODELABEL(switchcap), NULL, NULL, NULL, NULL, POST_KERNEL,
		 CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
