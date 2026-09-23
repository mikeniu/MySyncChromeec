/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "fan.h"
#include "hooks.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cros_cbi_ufsc_thermal_fan, LOG_LEVEL_INF);

#define POLICY_NODE DT_NODELABEL(fan_policy)

static void fan_ufsc_init(void)
{
	if (cros_cbi_ufsc_check_match(
		    CBI_UFSC_VALUE_ID(DT_PHANDLE(POLICY_NODE, enable_value)))) {
		LOG_INF("Thermal Fan: Enabled via UFSC Policy");
	} else {
		LOG_INF("Thermal Fan: Disabled (UFSC mismatch)");
		fan_set_count(0);
	}
}
DECLARE_HOOK(HOOK_INIT, fan_ufsc_init, HOOK_PRIO_POST_FIRST);
