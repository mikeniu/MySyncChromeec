/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)

#define PROCHOT_ASSERT_TEMP 95
#define PROCHOT_RELEASE_TEMP 85
#define PROCHOT_ASSERT_COUNT 10
#define PROCHOT_RELEASE_COUNT 5

#define TEMP_CHG TEMP_SENSOR_ID(DT_NODELABEL(temp_chg))
#define TEMP_BAT_CONN TEMP_SENSOR_ID(DT_NODELABEL(temp_bat_conn))

static uint8_t prochot_trigger_cnt;
static uint8_t prochot_release_cnt;
static bool prochot_asserted;

static int read_temp_c(int id)
{
	int temp;

	if (temp_sensor_read(id, &temp) != EC_SUCCESS)
		return -1;

	return K_TO_C(temp);
}

static void set_prochot(bool assert)
{
	if (assert == prochot_asserted)
		return;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_forcepr_odl_r),
			assert ? 0 : 1);
	prochot_asserted = assert;
	CPRINTS("%s prochot", assert ? "Assert" : "Release");
}

static void update_prochot(void)
{
	int temp_chg;
	int temp_bat_conn;
	bool over_limit;
	bool under_release;

	if (!chipset_in_state(CHIPSET_STATE_ON)) {
		prochot_trigger_cnt = 0;
		prochot_release_cnt = 0;
		set_prochot(false);
		return;
	}

	temp_chg = read_temp_c(TEMP_CHG);
	temp_bat_conn = read_temp_c(TEMP_BAT_CONN);
	over_limit = (temp_chg >= PROCHOT_ASSERT_TEMP) ||
		     (temp_bat_conn >= PROCHOT_ASSERT_TEMP);
	under_release =
		(temp_chg >= 0 && temp_chg < PROCHOT_RELEASE_TEMP) &&
		(temp_bat_conn >= 0 && temp_bat_conn < PROCHOT_RELEASE_TEMP);

	if (!prochot_asserted && over_limit) {
		if (++prochot_trigger_cnt >= PROCHOT_ASSERT_COUNT) {
			set_prochot(true);
			prochot_trigger_cnt = 0;
		}
		prochot_release_cnt = 0;
	} else if (prochot_asserted && under_release) {
		if (++prochot_release_cnt >= PROCHOT_RELEASE_COUNT) {
			set_prochot(false);
			prochot_release_cnt = 0;
		}
		prochot_trigger_cnt = 0;
	} else {
		prochot_trigger_cnt = 0;
		prochot_release_cnt = 0;
	}
}
DECLARE_HOOK(HOOK_SECOND, update_prochot, HOOK_PRIO_TEMP_SENSOR_DONE);
