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

	if (sb_read(SB_MANUFACTURER_ACCESS, &state) == 0) {
		/* Read successful. Check for damage flag. */
		if (state & BIT(12)) {
			return BP_NO;
		}
		return BP_YES;
	}

	/* Wait 25ms and try one more time */
	k_msleep(25);
	if (sb_read(SB_MANUFACTURER_ACCESS, &state) == 0) {
		if (state & BIT(12)) {
			return BP_NO;
		}
		return BP_YES;
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
