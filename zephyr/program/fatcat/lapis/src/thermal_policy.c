/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "extpower.h"
#include "hooks.h"
#include "power.h"
#include "temp_sensor/temp_sensor.h"
#include "usb_pd.h"

#define POLL_COUNT 5
#define CHARGER_LIMIT_LEVELS 4
#define TEMP_MAX 120
#define TEMP_MIN 0

static int current_limit;

static uint8_t charger_limit_level = 0;
static uint8_t charger_trigger_cnt = 0;
static uint8_t charger_release_cnt = 0;

typedef enum {
	LIMIT_NONE = 99999,
	LIMIT_2500 = 2500,
	LIMIT_1500 = 1500,
	LIMIT_1000 = 1000
} charge_limit_t;

typedef struct {
	uint8_t trigger_temp;
	uint8_t release_temp;
} temp_limit_t;

static const charge_limit_t charger_limit_table[CHARGER_LIMIT_LEVELS] = {
	LIMIT_NONE, LIMIT_2500, LIMIT_1500, LIMIT_1000
};

static const temp_limit_t charge_temp_limits[CHARGER_LIMIT_LEVELS - 1] = {
	{ 51, 48 },
	{ 53, 50 },
	{ 55, 52 }
};

static bool temp_is_valid(int temp)
{
	return temp > TEMP_MIN && temp < TEMP_MAX;
}

static void update_charge_limit(void)
{
	int charger_temp_k, charger_temp;

	temp_sensor_read(TEMP_SENSOR_ID(DT_NODELABEL(temp_charger)),
			 &charger_temp_k);

	charger_temp = K_TO_C(charger_temp_k);

	printk("--read charger_temp = %d\n", charger_temp);

	if (!temp_is_valid(charger_temp)) {
		return;
	}

	if (charger_limit_level < CHARGER_LIMIT_LEVELS - 1) {
		temp_limit_t chg = charge_temp_limits[charger_limit_level];

		if (charger_temp >= chg.trigger_temp) {
			if (++charger_trigger_cnt >= POLL_COUNT) {
				charger_limit_level++;
				charger_trigger_cnt = 0;
				charger_release_cnt = 0;
				printk("--level up -> %d\n", charger_limit_level);
			}
		} else {
			charger_trigger_cnt = 0;
		}
	}

	if (charger_limit_level > 0) {
		temp_limit_t chg = charge_temp_limits[charger_limit_level - 1];

		if (charger_temp < chg.release_temp) {
			if (++charger_release_cnt >= POLL_COUNT) {
				charger_limit_level--;
				charger_trigger_cnt = 0;
				charger_release_cnt = 0;
				printk("--level down -> %d\n", charger_limit_level);
			}
		} else {
			charger_release_cnt = 0;
		}
	}
}

static void update_current_limit(void)
{
	if (extpower_is_present() && chipset_in_state(CHIPSET_STATE_ON)) {
		update_charge_limit();
	}

	current_limit = charger_limit_table[charger_limit_level];
	printk("--charger_limit_level = %d\n", charger_limit_level);
	printk("--current_limit = %d\n", current_limit);
}
DECLARE_HOOK(HOOK_SECOND, update_current_limit, HOOK_PRIO_TEMP_SENSOR_DONE);

int charger_profile_override(struct charge_state_data *curr)
{
	curr->requested_current = min(curr->requested_current, current_limit);

	return EC_SUCCESS;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}
