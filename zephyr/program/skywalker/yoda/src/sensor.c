<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "accelgyro.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(yoda_sensor, LOG_LEVEL_INF);

enum lid_sensor_type {
	LID_LIS2DW12 = 0,
	LID_BMA422,
};

static int lid_use_alt_sensor;

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (lid_use_alt_sensor == LID_LIS2DW12)
		lis2dw12_interrupt(signal);
	else
		bma4xx_interrupt(signal);
}

test_export_static void alt_sensor_init(void)
{
	/* Check which motion sensors are used */
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_0)))) {
		LOG_INF("Lid : LIS2DWL");
		lid_use_alt_sensor = LID_LIS2DW12;
	} else {
		LOG_INF("Lid : BMA422");
		lid_use_alt_sensor = LID_BMA422;
	}
	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "accelgyro.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(yoda_sensor, LOG_LEVEL_INF);

enum base_sensor_type {
	BASE_LIS2DW12 = 0,
	BASE_BMA422,
};

enum lid_sensor_type {
	LID_LIS2DW12 = 0,
	LID_BMA422,
};

static int base_use_alt_sensor;
static int lid_use_alt_sensor;

void motion_interrupt(enum gpio_signal signal)
{
	if (lid_use_alt_sensor == LID_LIS2DW12)
		lis2dw12_interrupt(signal);
	else
		bma4xx_interrupt(signal);
}
void lid_accel_interrupt(enum gpio_signal signal)
{
	if (base_use_alt_sensor == BASE_LIS2DW12)
		lis2dw12_interrupt(signal);
	else
		bma4xx_interrupt(signal);
}

test_export_static void alt_sensor_init(void)
{
	/* Check which motion sensors are used */
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_0)))) {
		LOG_INF("Base : LIS2DWL");
		base_use_alt_sensor = BASE_LIS2DW12;
	} else {
		LOG_INF("Base : BMA422");
		base_use_alt_sensor = BASE_BMA422;
	}
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_0)))) {
		LOG_INF("Lid : LIS2DWL");
		lid_use_alt_sensor = LID_LIS2DW12;
	} else {
		LOG_INF("Lid : BMA422");
		lid_use_alt_sensor = LID_BMA422;
	}
	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
