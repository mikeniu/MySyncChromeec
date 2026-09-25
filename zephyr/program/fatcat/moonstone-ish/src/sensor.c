/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "cros_cbi.h"
#include "driver/accel_lis2dh_public.h"
#include "driver/accel_lis2dw12_public.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motionsense_sensors.h"

#include <zephyr/logging/log.h>

static int base_use_alt_sensor;

LOG_MODULE_REGISTER(moonstone_sensor, LOG_LEVEL_INF);

void motion_base_interrupt(enum gpio_signal signal)
{
	if (base_use_alt_sensor) {
		lis2dw12_interrupt(signal);
	} else {
		lis2dh_interrupt(signal);
	}
}

static void sensor_init(void)
{
	int id;

	base_use_alt_sensor = cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(ufsc_base_lis2dw12)));

	LOG_INF("sensor: base_accel = %s",
		base_use_alt_sensor ? "lis2dw12" : "lis2de12tr");

	motion_sensors_check_ufsc();

	/*
	 * If alt_sensors are not being used, then the lid_accel sensor, must
	 * use forced_mode and the interrupt must be disabled.
	 */
	if (!base_use_alt_sensor) {
		id = SENSOR_ID(DT_NODELABEL(lid_accel));
		motion_sensors[id].flags |= MOTIONSENSE_FLAG_IN_FORCED_MODE;
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_lid_accel));
		LOG_INF("sensor: lid sensor configured for forced_mode");
	}
}
DECLARE_HOOK(HOOK_INIT, sensor_init, HOOK_PRIO_POST_I2C);
