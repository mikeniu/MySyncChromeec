/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charger.h"
#include "timer.h"

#ifndef __CROS_EC_CHARGE_STATE_V2_H
#define __CROS_EC_CHARGE_STATE_V2_H

/* Delay after AP battery shutdown warning before we kill the AP */
#define LOW_BATTERY_SHUTDOWN_TIMEOUT_US (30 * SECOND)
/* Time to spend trying to wake a non-responsive battery */
#define PRECHARGE_TIMEOUT_US (30 * SECOND)

/* Power state task polling periods in usec */
#define POLL_PERIOD_VERY_LONG   MINUTE
#define POLL_PERIOD_LONG        (MSEC * 500)
#define POLL_PERIOD_CHARGE      (MSEC * 250)
#define POLL_PERIOD_SHORT       (MSEC * 100)
#define MIN_SLEEP_USEC          (MSEC * 50)
#define MAX_SLEEP_USEC          MINUTE

/*
 * The values exported by charge_get_state() and charge_get_flags() are used
 * only to control the LEDs (with one not-quite-correct exception). For V2
 * we use a different set of states internally.
 */
enum charge_state_v2 {
	ST_IDLE = 0,
	ST_DISCHARGE,
	ST_CHARGE,
	ST_PRECHARGE,

	NUM_STATES_V2
};

struct charge_state_data {
	timestamp_t ts;
	int ac;
	struct charger_params chg;
	struct batt_params batt;
	enum charge_state_v2 state;
	int requested_voltage;
	int requested_current;
};

/*
 * Optional customization.
 *
 * On input, the struct reflects the default behavior. The function can make
 * changes to the state, requested_voltage, or requested_current.
 *
 * Return value:
 *   >0    Desired time in usec for this poll period.
 *   0     Use the default poll period (which varies with the state).
 *  <0     An error occurred. The poll time will be shorter than usual. Too
 *           many errors in a row may trigger some corrective action.
 */
int charger_profile_override(struct charge_state_data *);

/*
 * Access to custom profile params through host commands.
 * What this does is up to the implementation.
 */
enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value);
enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value);

#endif /* __CROS_EC_CHARGE_STATE_V2_H */

