/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"

#include "charger.h"
#include "gpio.h"
#include "hooks.h"
#include "chipset.h"

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

static int ac_poweron_flag;
static int pwrbtn_poweron_flag;

enum battery_present battery_hw_present(void)
{
	static int first_poweron;

	first_poweron++;
	printk("---%d times on\n", first_poweron);

	if (ac_poweron_flag || pwrbtn_poweron_flag) {
		return BP_NO;
	}

	return BP_YES;
}

static void ac_poweron(void)
{
	if (!ac_poweron_flag) {
		ac_poweron_flag = 1;
		pwrbtn_poweron_flag = 1;
		chipset_power_on();
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, ac_poweron, HOOK_PRIO_DEFAULT);

static void powerbutton_poweron(void)
{
	if (!pwrbtn_poweron_flag) {
		pwrbtn_poweron_flag = 1;
		ac_poweron_flag = 1;
		chipset_power_on();
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, powerbutton_poweron, HOOK_PRIO_DEFAULT);

