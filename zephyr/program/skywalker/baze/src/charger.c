/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "charger_chips.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/charger/bq257x0_regs.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_pd.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

enum chg_opt {
	chg_opt_default = 0,
	chg_opt_rt9478,
	chg_opt_count,
};

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	if (battery_is_present() == BP_YES) {
		/*
		 * Limit current to 98% when AC+DC, for
		 * CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT is set to 2.
		 */
		charge_set_input_current_limit(charge_ma, charge_mv);
	} else {
		/* Limit current to 100% when AC only */
		charger_set_input_current_limit(0, charge_ma);
	}
}

void alt_charger_init(void)
{
	int charge_option = chg_opt_default;

	/* detect charger chip by ssfc in CBI */
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(charger_option_1)))) {
		charge_option = chg_opt_rt9478;
	}

	if (charge_option == chg_opt_rt9478) {
		ccprints("CHARGER IS RT9478");
		CHG_ENABLE_ALTERNATE(0);
	} else {
		ccprints("CHARGER IS BQ25720");
	}
}
DECLARE_HOOK(HOOK_INIT, alt_charger_init, HOOK_PRIO_POST_FIRST);
