/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accelgyro_lsm6dsv.h"
#include "driver/stm_mems_common.h"

#define CREATE_SENSOR_DATA_LSM6DSV(id, drvdata_name) \
	static struct stprivate_data drvdata_name;

CREATE_SENSOR_DATA(cros_ec_drvdata_lsm6dsv, CREATE_SENSOR_DATA_LSM6DSV)

CREATE_MOTION_SENSOR(cros_ec_lsm6dsv_accel, MOTIONSENSE_CHIP_LSM6DSV, \
		MOTIONSENSE_TYPE_ACCEL, lsm6dsv_drv, \
		LSM6DSV_ODR_MIN_VAL, LSM6DSV_ODR_MAX_VAL)
