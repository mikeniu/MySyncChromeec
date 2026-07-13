/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "timer.h"

enum battery_present battery_is_present(void)
{
	int state;

	for (int i = 0; i < 2; i++) {
		if (sb_read(SB_MANUFACTURER_ACCESS, &state) == 0) {
			/*
			 * According to the battery manufacturer's reply:
			 * To detect a bad battery, read the 0x00 register.
			 * If the 12th bit (Permanently Failure) is 1, it means
			 * a bad battery.
			 */
			return (state & BIT(12)) ? BP_NO : BP_YES;
		}
		if (i == 0) {
			k_msleep(25);
		}
	}

	/*
	 * Read failed twice.
	 * If we are within 10 seconds of system boot, return BP_YES to give
	 * the battery time to wake up and prevent the host from shutting down.
	 */
	if (get_time().val < (10 * USEC_PER_SEC)) {
		return BP_YES;
	}

	return BP_NO;
}
