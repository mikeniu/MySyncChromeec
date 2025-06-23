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
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/logging/log.h>

#define I2C_PORT_SENSOR 1

LOG_MODULE_REGISTER(uldrenite_sensor, LOG_LEVEL_INF);

static int sensor_fwconfig;

void motion_interrupt(enum gpio_signal signal)
{
	lis2dw12_interrupt(signal);
}

void lid_accel_interrupt(enum gpio_signal signal)
{
	lis2dw12_interrupt(signal);
}

static void motionsense_init(void)
{
	int ret;
	ret = cros_cbi_get_fw_config(FORM_FACTOR, &sensor_fwconfig);
	if (ret < 0) {
		LOG_ERR("error retriving CBI config: %d", ret);
		return;
	}
}
DECLARE_HOOK(HOOK_INIT, motionsense_init, HOOK_PRIO_DEFAULT);
