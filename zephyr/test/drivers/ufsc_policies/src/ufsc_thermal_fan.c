/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "fan.h"
#include "hooks.h"
#include "ufsc_policies_test.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define VALUE_NODE_PRESENT DT_NODELABEL(ufsc_thermal_fan_present)

static bool mock_ufsc_thermal_fan_present;

static bool mock_cbi_ufsc_check_match_fan(enum cbi_ufsc_value_id value_id)
{
	if (value_id == CBI_UFSC_VALUE_ID(VALUE_NODE_PRESENT)) {
		return mock_ufsc_thermal_fan_present;
	}
	return false;
}

static void ufsc_thermal_fan_before(void *data)
{
	ARG_UNUSED(data);

	cros_cbi_ufsc_check_match_fake.custom_fake =
		mock_cbi_ufsc_check_match_fan;

	/* Reset fan count to default CONFIG_FANS */
	fan_set_count(CONFIG_FANS);
}

ZTEST_SUITE(ufsc_thermal_fan, NULL, NULL, ufsc_thermal_fan_before, NULL, NULL);

ZTEST(ufsc_thermal_fan, test_fan_present)
{
	mock_ufsc_thermal_fan_present = true;

	hook_notify(HOOK_INIT);
	zassert_equal(fan_get_count(), 1, "fan count should remain 1");
}

ZTEST(ufsc_thermal_fan, test_fan_absent)
{
	mock_ufsc_thermal_fan_present = false;

	hook_notify(HOOK_INIT);
	zassert_equal(fan_get_count(), 0, "fan count should be set to 0");
}
