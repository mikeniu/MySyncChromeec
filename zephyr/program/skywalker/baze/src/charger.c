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
#include "driver/charger/bq257x0_regs.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "usb_pd.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#define CHARGER_REG_MANUFACTURER_ID 0xfe
#define CHARGER_REG_DEVICE_ID 0xff
#define BQ25720_MANUF_ID 0x0040
#define ISL9238C_MANUF_ID 0x0049
#define ISL9238C_DEVICE_ID 0x000c

enum chg_opt {
	chg_opt_default = 0,
	chg_opt_isl9238c,
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
	int rv, manuf_id, dev_id;
	/* detect charger chip by manuf ID and Device ID */
	rv = i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			CHARGER_REG_MANUFACTURER_ID, &manuf_id);
	if (rv) {
		ccprints("Not read manuf id from charger!");
	} else {
		ccprints("charger manuf ID=0x%x", manuf_id);
	}

	rv = i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			CHARGER_REG_DEVICE_ID, &dev_id);
	if (rv) {
		ccprints("Not read device id from charger!");
	} else {
		ccprints("charger device ID=0x%x", dev_id);
	}

	if (manuf_id == ISL9238C_MANUF_ID) {
		charge_option = chg_opt_isl9238c;
	}

	if (charge_option == chg_opt_isl9238c) {
		ccprints("CHARGER IS ISL9238C");
		CHG_ENABLE_ALTERNATE(0);
	} else {
		ccprints("CHARGER IS BQ25720");
	}
}
DECLARE_HOOK(HOOK_INIT, alt_charger_init, HOOK_PRIO_POST_FIRST);
