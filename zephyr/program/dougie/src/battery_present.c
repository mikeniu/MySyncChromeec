/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "battery.h"

/* TODO: battery present is routed to GSC */

/*
 * Physical detection of battery.
 */
enum battery_present battery_hw_present(void)
{
	return BP_NOT_SURE;
}

enum battery_present battery_is_present(void)
{
	return BP_NOT_SURE;
}

