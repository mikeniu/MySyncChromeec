/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "hooks.h"
#include "tablet_mode.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cros_cbi_ufsc_tablet_mode, LOG_LEVEL_INF);

#define POLICY_NODE DT_NODELABEL(tablet_mode_policy)

static void tablet_mode_ufsc_init(void)
{
	if (cros_cbi_ufsc_check_match(
		    CBI_UFSC_VALUE_ID(DT_PHANDLE(POLICY_NODE, enable_value)))) {
		LOG_INF("Tablet Mode: Enabled (Convertible Form Factor)");
	} else {
		LOG_INF("Tablet Mode: Disabled (Clamshell / Fixed Form Factor)");
		gmr_tablet_switch_disable();
	}
}
DECLARE_HOOK(HOOK_INIT, tablet_mode_ufsc_init, HOOK_PRIO_POST_I2C);
