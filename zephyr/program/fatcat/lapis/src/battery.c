/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "bbram.h"
#include "chipset.h"
#include "extpower.h"
#include "hooks.h"
#include "power_button.h"

#include <zephyr/drivers/bbram.h>

enum battery_present battery_is_present(void)
{
	static int retry_cnt;
	int state;

	if (sb_read(SB_MANUFACTURER_ACCESS, &state)) {
		/* Require 2 consecutive failures before declaring the
		 * battery missing.
		 */
		k_msleep(25);
		if (sb_read(SB_MANUFACTURER_ACCESS, &state)) {
			if (retry_cnt > 100) {
				return BP_NO;
			} else {
				retry_cnt++;

				return BP_YES;
			}
		}
	}

	retry_cnt = 0;

	/*
	 *  According to the battery manufacturer's reply:
	 *  To detect a bad battery, need to read the 0x00 register.
	 *  If the 12th bit(Permanently Failure) is 1, it means a bad battery.
	 */
	if (state & BIT(12)) {
		return BP_NO;
	}

	return BP_YES;
}

static const struct device *const bbram_dev =
	COND_CODE_1(DT_HAS_CHOSEN(cros_ec_bbram),
		    DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram)), NULL);

static int poweron_flag;

static void check_first_boot(void)
{
	uint32_t board_batt_keep_val = 0;

	bbram_read(bbram_dev, BBRAM_REGION_OFFSET(board_batt_keep),
		   BBRAM_REGION_SIZE(board_batt_keep),
		   (uint8_t *)&board_batt_keep_val);

	if (board_batt_keep_val == 0xA5A5A5A5) {
		poweron_flag = 1;
		printk("---booted before\n");
	} else {
		board_batt_keep_val = 0xA5A5A5A5;
		bbram_write(bbram_dev, BBRAM_REGION_OFFSET(board_batt_keep),
			    BBRAM_REGION_SIZE(board_batt_keep),
			    (uint8_t *)&board_batt_keep_val);
		poweron_flag = 0;
		printk("---first active\n");
	}
}
DECLARE_HOOK(HOOK_INIT, check_first_boot, HOOK_PRIO_DEFAULT + 1);

static void first_power_on(void)
{
	if (!poweron_flag)
		chipset_power_on();
}
DECLARE_HOOK(HOOK_AC_CHANGE, first_power_on, HOOK_PRIO_DEFAULT);

static void update_poweron_flag(void)
{
	if (!poweron_flag) {
		poweron_flag = 1;
		printk("---clear power on flag\n");
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, update_poweron_flag, HOOK_PRIO_LAST);

int custom_prevent_power_on(void)
{
	static int try_cnt;

	try_cnt++;
	printk("---%d times try\n", try_cnt);

	/* Allow power on if power button pressed or AC attached */
	if (!poweron_flag &&
	    (power_button_is_pressed() || extpower_is_present()))
		poweron_flag = 1;

	printk("---%s\n", poweron_flag ? "can power on" : "NO power on");
	return !poweron_flag;
}
