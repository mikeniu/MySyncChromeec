/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "timer.h"

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
			/*
			 * Limit the 100-retry spoof (2.5s) to the boot phase
			 * (first 10 seconds) to allow slow-waking batteries to
			 * respond. During runtime (e.g. S0ix), only allow 2
			 * retries to prevent I2C timeout cascades from starving
			 * the watchdog.
			 */
			int max_retries =
				(get_time().val < 10 * USEC_PER_SEC) ? 100 : 2;
			if (retry_cnt > max_retries) {
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
