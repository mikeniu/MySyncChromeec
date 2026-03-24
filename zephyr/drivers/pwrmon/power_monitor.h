/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_
#define ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

/**
 * @brief Get the attribute and value to enable/disable a power monitor channel.
 *
 * @param val Pointer to sensor_value to store the enable/disable value.
 * @param enable True to enable the channel, false to disable.
 * @return The sensor_attribute for channel enable, or negative if not
 * supported.
 */
enum sensor_attribute
power_monitor_channel_enable_attr(struct sensor_value *val, bool enable);

/**
 * @brief Map a channel ID to the corresponding sensor channel.
 *
 * @param channel_id The ID of the channel.
 * @return The mapped sensor_channel.
 */
enum sensor_channel power_monitor_sensor_channel(int channel_id);

/**
 * @brief Get the sensor channel used for reading the sample count.
 *
 * @return The sensor_channel for sample count.
 */
enum sensor_channel power_monitor_sample_count_channel(void);

/**
 * @brief Get the attribute and value to latch the power monitor accumulators.
 *
 * @param val Pointer to sensor_value to store the latch command value.
 * @return The sensor_attribute for latching, or negative if mot supported.
 */
enum sensor_attribute power_monitor_latch_attr(struct sensor_value *val);

#endif /* ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_ */
