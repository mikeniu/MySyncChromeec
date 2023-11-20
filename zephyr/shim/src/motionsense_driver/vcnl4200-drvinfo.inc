/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/als_vcnl4200.h"

/*
 * CREATE_SENSOR_DATA which is defined in motionsense_sensros.c is
 * the helper to create sensor driver specific data.
 *
 * CREATE_SENSOR_DATA gets two arguments. One is the compatible
 * property value specified in device tree and the other one is the macro
 * that actually creates sensor driver specific data. The macro gets
 * node id and the name to be used for the sensor driver data.
 */

/* include macros for common data strutures from accelgyro.h */


/* Create driver data for vcnl4200 driver. */
#define CREATE_SENSOR_DATA_VCNL4200(id, drvdata_name) \
	static struct vcnl4200_drv_data drvdata_name;

/*
 * Create driver data for each VCNL4200 drvdata instance in device tree.
 * (compatible = "cros-ec,drvdata-vcnl4200")
 */
CREATE_SENSOR_DATA(cros_ec_drvdata_vcnl4200, CREATE_SENSOR_DATA_VCNL4200)

/*
 * CREATE_MOTION_SENSOR which is defined in motionsense_sensros.c is
 * the macro to create an entry in motion_sensors array.
 * The macro gets value of compatible property of
 * the sensor in device tree and sensor specific values like chip ID,
 * type of sensor, name of driver, default min/max frequency.
 * Then using the values, it creates the corresponding motion_sense_t entry
 * in motion_sensors array.
 */

/*
 * Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * for each VCNL4200 als instance (compatible = "cros-ec,vcnl4200")
 * in device tree.
 */
CREATE_MOTION_SENSOR(cros_ec_vcnl4200, MOTIONSENSE_CHIP_VCNL4200,       \
		     MOTIONSENSE_TYPE_PROX, vcnl4200_drv,               \
		     VCNL4200_LIGHT_MIN_FREQ, VCNL4200_LIGHT_MAX_FREQ)
