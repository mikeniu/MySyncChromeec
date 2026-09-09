/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "hooks.h"
#include "tablet_mode.h"
#include "ufsc_policies_test.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define VALUE_NODE_CONVERTIBLE DT_NODELABEL(ufsc_form_factor_convertible)

static bool mock_ufsc_form_factor_convertible;

static bool mock_cbi_ufsc_check_match_tablet(enum cbi_ufsc_value_id value_id)
{
	if (value_id == CBI_UFSC_VALUE_ID(VALUE_NODE_CONVERTIBLE)) {
		return mock_ufsc_form_factor_convertible;
	}
	return false;
}

static void ufsc_tablet_mode_before(void *data)
{
	ARG_UNUSED(data);

	cros_cbi_ufsc_check_match_fake.custom_fake =
		mock_cbi_ufsc_check_match_tablet;

	tablet_reset();
}

ZTEST_SUITE(ufsc_tablet_mode, NULL, NULL, ufsc_tablet_mode_before, NULL, NULL);

ZTEST(ufsc_tablet_mode, test_tablet_mode_convertible)
{
	mock_ufsc_form_factor_convertible = true;

	hook_notify(HOOK_INIT);
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(tablet_get_mode(), 1,
		      "tablet mode should be enabled for convertible");
}

ZTEST(ufsc_tablet_mode, test_tablet_mode_clamshell)
{
	mock_ufsc_form_factor_convertible = false;

	hook_notify(HOOK_INIT);
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(tablet_get_mode(), 0,
		      "tablet mode should remain disabled for clamshell");
}
