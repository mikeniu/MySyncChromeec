/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "driver/charger/bq25710.h"
#include "driver/charger/bq257x0_regs.h"
#include "hooks.h"
#include "i2c.h"
#include "usbc/pdc_power_mgmt.h"
#include "util.h"

#define BATT_2_CELL 2
#define BATT_3_CELL 3
#define BATT_2_CELL_VSYS 6600
#define BATT_3_CELL_VSYS 9200
#define RETRY_CHARGER 3

/* 2cell battery */
#define MODEL_CP856931 "CP856931"
#define MODEL_CP87886601 "CP87886601"
#define MODEL_SB1FB00013 "SB1FB00013"

static uint8_t pre_battery_cells = BATT_3_CELL;
static bool check_charger_state = false;
static uint8_t retry_charger_cyc = 0;

static inline int min_system_reg_to_voltage_mv(int reg)
{
	int steps;
	steps = GET_BQ_FIELD(BQ25720, VSYS_MIN, VOLTAGE, reg);
	return steps * BQ25720_VSYS_MIN_VOLTAGE_STEP_MV;
}

static bool is_2s_battery(const char *model)
{
	return !strncasecmp(model, MODEL_CP856931, strlen(MODEL_CP856931)) ||
	       !strncasecmp(model, MODEL_CP87886601,
			    strlen(MODEL_CP87886601)) ||
	       !strncasecmp(model, MODEL_SB1FB00013, strlen(MODEL_SB1FB00013));
}

void battery_policy(void)
{
	if (battery_is_present() != BP_YES)
		return;

	struct battery_static_info *bs = &battery_static[BATT_IDX_MAIN];

	if (!bs->model_ext[0]) {
		update_static_battery_info();
	}

	if (!is_2s_battery(bs->model_ext)) {
		/* 3-cell*/
		if (pre_battery_cells != BATT_3_CELL) {
			pre_battery_cells = BATT_3_CELL;
			check_charger_state = true;
			retry_charger_cyc = 0;
		}
	} else {
		/* 2-cell*/
		if (pre_battery_cells != BATT_2_CELL) {
			pre_battery_cells = BATT_2_CELL;
			check_charger_state = true;
			retry_charger_cyc = 0;
		}
	}

	if (check_charger_state == true) {
		int value, vsys;
		bq25710_set_min_system_voltage(
			0, pre_battery_cells == BATT_2_CELL ? BATT_2_CELL_VSYS :
							      BATT_3_CELL_VSYS);
		if (i2c_read16(chg_chips[0].i2c_port,
			       chg_chips[0].i2c_addr_flags,
			       BQ25710_REG_MIN_SYSTEM_VOLTAGE, &value)) {
			ccprints("charger read failed");
			if (retry_charger_cyc < RETRY_CHARGER) {
				retry_charger_cyc++;
				if (retry_charger_cyc == RETRY_CHARGER) {
					pdc_power_mgmt_set_comms_state(false);
				}
			}
			return;
		}

		vsys = min_system_reg_to_voltage_mv(value);

		if (pre_battery_cells == BATT_2_CELL &&
		    vsys >= BATT_2_CELL_VSYS && vsys <= BATT_3_CELL_VSYS) {
			check_charger_state = false;
			if (retry_charger_cyc == RETRY_CHARGER) {
				pdc_power_mgmt_set_comms_state(true);
			}
			return;
		} else if (pre_battery_cells == BATT_3_CELL &&
			   vsys >= BATT_3_CELL_VSYS) {
			check_charger_state = false;
			if (retry_charger_cyc == RETRY_CHARGER) {
				pdc_power_mgmt_set_comms_state(true);
			}
			return;
		} else {
			ccprints("charger vsys set %d but read %d",
				 pre_battery_cells == BATT_2_CELL ?
					 BATT_2_CELL_VSYS :
					 BATT_3_CELL_VSYS,
				 vsys);
			if (retry_charger_cyc < RETRY_CHARGER) {
				retry_charger_cyc++;
				if (retry_charger_cyc == RETRY_CHARGER) {
					pdc_power_mgmt_set_comms_state(false);
				}
			}
		}
	}
}

void ac_charge_policy(void)
{
	int value, vsys;
	if (i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		       BQ25710_REG_MIN_SYSTEM_VOLTAGE, &value)) {
		check_charger_state = true;
		retry_charger_cyc = 0;
		return;
	}

	vsys = min_system_reg_to_voltage_mv(value);

	if (pre_battery_cells == BATT_2_CELL && vsys >= BATT_2_CELL_VSYS &&
	    vsys <= BATT_3_CELL_VSYS)
		return;
	else if (pre_battery_cells == BATT_3_CELL && vsys >= BATT_3_CELL_VSYS)
		return;
	else {
		ccprints("charger vsys set %d but read %d",
			 pre_battery_cells == BATT_2_CELL ? BATT_2_CELL_VSYS :
							    BATT_3_CELL_VSYS,
			 vsys);
		check_charger_state = true;
		retry_charger_cyc = 0;
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, ac_charge_policy, HOOK_PRIO_DEFAULT);

int charger_profile_override(struct charge_state_data *curr)
{
	battery_policy();
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
