/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "gpio/gpio.h"
#include "hooks.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>

#define TEMP_CHG TEMP_SENSOR_ID(DT_NODELABEL(temp_chg))
#define TEMP_BAT_CONN TEMP_SENSOR_ID(DT_NODELABEL(temp_bat_conn))
#define TEMP_TC_MB TEMP_SENSOR_ID(DT_NODELABEL(temp_tc_mb))
#define TEMP_TC_DB TEMP_SENSOR_ID(DT_NODELABEL(temp_tc_db))

#define FORCEPR_THRESHOLD_ON_TEMP 95
#define FORCEPR_THRESHOLD_OFF_TEMP 85
#define PD_DISABLE_THRESHOLD_TEMP 80
#define PD_RESET_THRESHOLD_TEMP 60

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
struct temp_debounce {
	int trigger_count;
	int release_count;
	int cnt;
	bool active;
};

static bool temp_debounce_update(struct temp_debounce *d, bool trig, bool rel)
{
	bool condition = d->active ? rel : trig;

	if (!condition) {
		d->cnt = 0;
	} else if (++d->cnt >=
		   (d->active ? d->release_count : d->trigger_count)) {
		d->active = !d->active;
		d->cnt = 0;
	}

	return d->active;
}

static struct temp_debounce forcepr_debounce = {
	.trigger_count = 10,
	.release_count = 5,
	.active = 0,
};

static struct temp_debounce pd_debounce = {
	.trigger_count = 10,
	.release_count = 5,
};

struct system_temps {
	int chg;
	int bat_conn;
	int tc_mb;
	int tc_db;
};

static struct system_temps temp_update(void)
{
	struct system_temps t;
	int t_k;

	t.chg = 0;
	t.bat_conn = 0;
	t.tc_mb = 0;
	t.tc_db = 0;

	if (temp_sensor_read(TEMP_CHG, &t_k) == EC_SUCCESS)
		t.chg = K_TO_C(t_k);
	if (temp_sensor_read(TEMP_BAT_CONN, &t_k) == EC_SUCCESS)
		t.bat_conn = K_TO_C(t_k);
	if (temp_sensor_read(TEMP_TC_MB, &t_k) == EC_SUCCESS)
		t.tc_mb = K_TO_C(t_k);
	if (temp_sensor_read(TEMP_TC_DB, &t_k) == EC_SUCCESS)
		t.tc_db = K_TO_C(t_k);

	return t;
}

static void system_thermal_protect(void)
{
	const struct system_temps t = temp_update();
	static bool forcepr_last;
	bool forcepr_on = temp_debounce_update(
		&forcepr_debounce,
		(t.chg >= FORCEPR_THRESHOLD_ON_TEMP) ||
			(t.bat_conn >= FORCEPR_THRESHOLD_ON_TEMP),
		(t.chg < FORCEPR_THRESHOLD_OFF_TEMP) &&
			(t.bat_conn < FORCEPR_THRESHOLD_OFF_TEMP));
	if (forcepr_on != forcepr_last) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_forcepr_odl_r),
				!forcepr_on);
		forcepr_last = forcepr_on;
	}
	bool pd_disable = temp_debounce_update(
		&pd_debounce,
		(t.tc_mb >= PD_DISABLE_THRESHOLD_TEMP) ||
			(t.tc_db >= PD_DISABLE_THRESHOLD_TEMP),
		(t.tc_mb < PD_RESET_THRESHOLD_TEMP) &&
			(t.tc_db < PD_RESET_THRESHOLD_TEMP));

	if (pd_disable)
		;
}
DECLARE_HOOK(HOOK_SECOND, system_thermal_protect, HOOK_PRIO_TEMP_SENSOR_DONE);
