/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "power_monitor.h"

#include <zephyr/drivers/sensor.h>

enum sensor_attribute
power_monitor_channel_enable_attr(struct sensor_value *val, bool enable)
{
	return -1;
}

enum sensor_channel power_monitor_sensor_channel(int channel_id)
{
	return SENSOR_CHAN_POWER;
}

enum sensor_channel power_monitor_sample_count_channel(void)
{
	return SENSOR_CHAN_FREQUENCY;
}

enum sensor_attribute power_monitor_latch_attr(struct sensor_value *val)
{
	return -1;
}
