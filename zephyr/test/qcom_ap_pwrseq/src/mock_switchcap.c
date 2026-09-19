/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock_switchcap.h"

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/fff.h>

#include <drivers/cros_switchcap.h>

DEFINE_FAKE_VALUE_FUNC(int, cros_switchcap_set_power, const struct device *,
		       bool);
DEFINE_FAKE_VALUE_FUNC(bool, cros_switchcap_is_enabled, const struct device *);
DEFINE_FAKE_VALUE_FUNC(bool, cros_switchcap_is_power_good,
		       const struct device *);

static int custom_cros_switchcap_set_power(const struct device *dev,
					   bool enable)
{
	ARG_UNUSED(dev);

	cros_switchcap_is_enabled_fake.return_val = enable;
	return 0;
}

void mock_switchcap_reset_state(void)
{
	RESET_FAKE(cros_switchcap_set_power);
	RESET_FAKE(cros_switchcap_is_enabled);
	RESET_FAKE(cros_switchcap_is_power_good);
	cros_switchcap_set_power_fake.custom_fake =
		custom_cros_switchcap_set_power;
	cros_switchcap_is_power_good_fake.return_val = true;
}

DEVICE_DT_DEFINE(DT_NODELABEL(switchcap), NULL, NULL, NULL, NULL, POST_KERNEL,
		 CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
