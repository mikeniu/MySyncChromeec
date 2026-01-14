/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "fpsensor/fpsensor_btn_ign.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <cstdint>
#include <mkbp_event.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

/* Define mocks for the functions declared in drivers/btn_ign.h */
FAKE_VOID_FUNC(btn_ign_activate);
FAKE_VOID_FUNC(btn_ign_deactivate);

static void fpsensor_btn_ign_before(void *data)
{
	RESET_FAKE(btn_ign_activate);
	RESET_FAKE(btn_ign_deactivate);
}

ZTEST_SUITE(fpsensor_btn_ign, NULL, NULL, fpsensor_btn_ign_before, NULL, NULL);

ZTEST(fpsensor_btn_ign, test_update_btn_ign_enroll)
{
	update_btn_ign(FP_MODE_ENROLL_SESSION);

	zassert_equal(btn_ign_activate_fake.call_count, 1,
		      "btn_ign_activate should be called");
	zassert_equal(btn_ign_deactivate_fake.call_count, 0,
		      "btn_ign_deactivate should not be called");
}

ZTEST(fpsensor_btn_ign, test_update_btn_ign_match)
{
	update_btn_ign(FP_MODE_MATCH);

	zassert_equal(btn_ign_activate_fake.call_count, 1,
		      "btn_ign_activate should be called");
	zassert_equal(btn_ign_deactivate_fake.call_count, 0,
		      "btn_ign_deactivate should not be called");
}

ZTEST(fpsensor_btn_ign, test_update_btn_ign_combined)
{
	update_btn_ign(FP_MODE_ENROLL_SESSION | FP_MODE_MATCH);

	zassert_equal(btn_ign_activate_fake.call_count, 1,
		      "btn_ign_activate should be called");
	zassert_equal(btn_ign_deactivate_fake.call_count, 0,
		      "btn_ign_deactivate should not be called");
}

ZTEST(fpsensor_btn_ign, test_update_btn_ign_other)
{
	update_btn_ign(0);

	zassert_equal(btn_ign_activate_fake.call_count, 0,
		      "btn_ign_activate should not be called");
	zassert_equal(btn_ign_deactivate_fake.call_count, 1,
		      "btn_ign_deactivate should be called");

	update_btn_ign(FP_MODE_RESET_SENSOR);

	zassert_equal(btn_ign_activate_fake.call_count, 0,
		      "btn_ign_activate should not be called");
	zassert_equal(btn_ign_deactivate_fake.call_count, 2,
		      "btn_ign_deactivate should be called");
}
