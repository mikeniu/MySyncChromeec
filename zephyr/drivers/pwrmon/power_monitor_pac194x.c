/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "power_monitor.h"

#include <zephyr/drivers/sensor/pac194x.h>

enum sensor_attribute
power_monitor_channel_enable_attr(struct sensor_value *val, bool enable)
{
	val->val1 = enable;
	return (enum sensor_attribute)SENSOR_ATTR_CHANNEL_ENABLED;
}

enum sensor_channel power_monitor_sensor_channel(int channel_id)
{
	return (enum sensor_channel)(PAC194X_CHAN_ACC1_AVG + channel_id);
}

enum sensor_channel power_monitor_sample_count_channel(void)
{
	return (enum sensor_channel)PAC194X_CHAN_ACC_COUNT;
}

enum sensor_attribute power_monitor_latch_attr(struct sensor_value *val)
{
	val->val1 = PAC194X_SENSOR_ATTR_FORCE_REFRESH_CMD_SINGLE;
	return (enum sensor_attribute)SENSOR_ATTR_FORCE_REFRESH_CMD;
}
