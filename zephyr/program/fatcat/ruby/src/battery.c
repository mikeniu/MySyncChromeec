/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "hooks.h"
#include "timer.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

#define SHIP_MODE_REG 0x34
#define TURN_ON_FET_CMD_FIRST 0x2000
#define TURN_ON_FET_CMD_SECOND 0x4000

static void ruby_battery_write_second_command(void)
{
	int rv;

	rv = sb_write(SHIP_MODE_REG, TURN_ON_FET_CMD_SECOND);
	if (rv) {
		LOG_ERR("Battery write 0x%04x result: %d",
			TURN_ON_FET_CMD_SECOND, rv);
	}
	battery_cutoff_set_state(BATTERY_CUTOFF_STATE_NORMAL);
}
DECLARE_DEFERRED(ruby_battery_write_second_command);

static void ruby_battery_init(void)
{
	int battery_data;
	int rv;

	if (battery_is_present() != BP_YES) {
		return;
	}

	if (sb_read(SB_MANUFACTURE_DATE, &battery_data)) {
		return;
	}

	battery_cutoff_set_state(BATTERY_CUTOFF_STATE_IN_PROGRESS);
	rv = sb_write(SHIP_MODE_REG, TURN_ON_FET_CMD_FIRST);
	if (rv) {
		battery_cutoff_set_state(BATTERY_CUTOFF_STATE_NORMAL);
		return;
	}

	hook_call_deferred(&ruby_battery_write_second_command_data,
			   1 * USEC_PER_SEC);
}

DECLARE_HOOK(HOOK_INIT, ruby_battery_init, HOOK_PRIO_POST_DEFAULT);
