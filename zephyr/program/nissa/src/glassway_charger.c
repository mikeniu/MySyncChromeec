/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charger.h"
#include "driver/charger/isl923x.h"
#include "i2c.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static int get_battery_cells(void)
{
	int val, battery_cells;

	if (i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		       ISL9238_REG_INFO2, &val) == EC_SUCCESS) {
		/* PROG resister read out. Number of battery cells [4:0] */
		val = val & 0x001f;
	}

	if (val == 0 || val >= 0x18)
		battery_cells = 1;
	else if (val >= 0x01 && val <= 0x08)
		battery_cells = 2;
	else if (val >= 0x09 && val <= 0x10)
		battery_cells = 3;
	else
		battery_cells = 4;

	return battery_cells;
}

__override int board_get_default_battery_type(void)
{
	int cells;
	int type = DEFAULT_BATTERY_TYPE;

	cells = get_battery_cells();
	LOG_INF("Get battery cells: %d", cells);

	if (cells == 3)
		type = DEFAULT_BATTERY_TYPE_3S;

	return type;
}
