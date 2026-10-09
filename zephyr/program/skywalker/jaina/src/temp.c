/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "common.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(skywalker_temp, LOG_LEVEL_INF);
/*
 * battery unit is deciKelvin (0.1 K).
 * 0°C = 273.1 K = 2731 dK
 * 16°C = 2731 + 160 = 2891 dK
 */
#define BATT_TEMP_LOW_LIMIT_DK 2891
#define BATT_CHARGE_CURRENT_LIMIT_MA 1824

int charger_profile_override(struct charge_state_data *curr)
{
	static bool is_current_limited = false;

	if (curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE)
		return 0;
	/* If battery temps < 16 degree, set the charge current = 1824ma */
	if (curr->batt.temperature < BATT_TEMP_LOW_LIMIT_DK) {
		if (curr->requested_current > BATT_CHARGE_CURRENT_LIMIT_MA) {
			curr->requested_current = BATT_CHARGE_CURRENT_LIMIT_MA;
			if (!is_current_limited) {
				LOG_INF("BATT: Temp < 16C, limiting charge current to %dmA",
					curr->requested_current);
				is_current_limited = true;
			}
		}
	} else {
		if (is_current_limited) {
			LOG_INF("BATT: Temp recovered, lifting current limit");
			is_current_limited = false;
		}
	}

	return 0;
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
