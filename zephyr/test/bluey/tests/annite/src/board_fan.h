/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_BLUEY_TESTS_ANNITE_SRC_BOARD_FAN_H_
#define PLATFORM_EC_ZEPHYR_TEST_BLUEY_TESTS_ANNITE_SRC_BOARD_FAN_H_

#include "temp_sensor/temp_sensor.h"

#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_cpu))

int fan_table_to_rpm(int fan, int *temp);
void board_override_fan_control(int fan, int *temp);

#endif /* PLATFORM_EC_ZEPHYR_TEST_BLUEY_TESTS_ANNITE_SRC_BOARD_FAN_H_ */
