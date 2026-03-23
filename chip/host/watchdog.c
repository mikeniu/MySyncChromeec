/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "watchdog.h"

/* Empty watchdog stubs for host tests */

int watchdog_init(void)
{
	return EC_SUCCESS;
}

void chip_watchdog_reload(void)
{
}
