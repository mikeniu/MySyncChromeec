/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "driver/charger/bq25710.h"
#include "hooks.h"
#include "util.h"

#define BATT_UNKNOWN 0
#define BATT_2_CELLS 2
#define BATT_3_CELLS 3

static uint8_t pre_battery_cells = BATT_UNKNOWN;

static const char *p2s_battery_models[] = {
	"l26n2pk1", // MODEL_L26N2PK1
	"l26b2pk1", // MODEL_L26B2PK1
	"l26x2pk1", // MODEL_L26X2PK1
	"l26m2pk1", // MODEL_L26M2PK1
	"l26d2pk1", // MODEL_L26D2PK1
};

int pd_get_usb_pd_3a_ports(void)
{
	return (pre_battery_cells == BATT_2_CELLS) ? 0 : 1;
}

static bool is_2s_battery(const char *model)
{
	for (int i = 0; i < ARRAY_SIZE(p2s_battery_models); i++) {
		int len = strlen(p2s_battery_models[i]);
		if (strncasecmp(model, p2s_battery_models[i], len) == 0) {
			return true;
		}
	}
	return false;
}

static int write_reg(int reg, int val)
{
	return bq257x0_set_option_reg(charge_get_active_chg_chip(), reg, val);
}

static void set_chg_reg_custom(int battery_cell)
{
	switch (battery_cell) {
	case BATT_2_CELLS: {
		// Address: 0x37, Voltage: 6.0 V
		write_reg(BQ25720_REG_VMIN_ACTIVE_PROTECTION, 0x0070);
		// Address: 0x3E, Voltage: 6.6 V
		write_reg(BQ25710_REG_MIN_SYSTEM_VOLTAGE, 0x4200);
		break;
	}
	case BATT_3_CELLS: {
		// Address: 0x37, Voltage: 9.0 V
		write_reg(BQ25720_REG_VMIN_ACTIVE_PROTECTION, 0x00e8);
		// Address: 0x3E, Voltage: 9.2 V
		write_reg(BQ25710_REG_MIN_SYSTEM_VOLTAGE, 0x5c00);
		break;
	}
	default:
		return;
	}
}

void battery_policy(void)
{
	if (battery_is_present() != BP_YES) {
		pre_battery_cells = BATT_UNKNOWN;
		return;
	}

	struct battery_static_info *bs = &battery_static[BATT_IDX_MAIN];
	uint8_t curr_battery_cells =
		is_2s_battery(bs->model_ext) ? BATT_2_CELLS : BATT_3_CELLS;
	if (curr_battery_cells != pre_battery_cells) {
		set_chg_reg_custom(curr_battery_cells);
		pre_battery_cells = curr_battery_cells;
	}
}
DECLARE_HOOK(HOOK_INIT, battery_policy, HOOK_PRIO_POST_BATTERY_INIT + 1);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, battery_policy, HOOK_PRIO_DEFAULT);
