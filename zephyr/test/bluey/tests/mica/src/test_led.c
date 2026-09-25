/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "lb_policy.h"
#include "led_common.h"
#include "stubs.h"

#include <zephyr/ztest.h>

enum ec_status board_lightbar_custom_seq(uint8_t seq);
void board_diag_led_power_fail(void);

static void test_led_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(lb_set_diag_policy);
}

ZTEST_SUITE(mica_led, NULL, NULL, test_led_before, NULL, NULL);

/* Test custom lightbar animation sequence for ramdump */
ZTEST(mica_led, test_lightbar_custom_seq)
{
	zassert_equal(board_lightbar_custom_seq(LIGHTBAR_CMD_SEQ_RAMDUMP),
		      EC_RES_SUCCESS);
	zassert_equal(lb_set_diag_policy_fake.call_count, 1);
	zassert_equal(lb_set_diag_policy_fake.arg0_val,
		      LED_ALT_POLICY_DIAG_RAMDUMP);
	zassert_equal(lb_set_diag_policy_fake.arg1_val, 90000);

	RESET_FAKE(lb_set_diag_policy);
	zassert_equal(board_lightbar_custom_seq(LIGHTBAR_CMD_SEQ_DIAG_CLEAR),
		      EC_RES_SUCCESS);
	zassert_equal(lb_set_diag_policy_fake.call_count, 1);
	zassert_equal(lb_set_diag_policy_fake.arg0_val, LED_ALT_POLICY_NORMAL);
	zassert_equal(lb_set_diag_policy_fake.arg1_val, 0);

	RESET_FAKE(lb_set_diag_policy);
	zassert_equal(board_lightbar_custom_seq(LIGHTBAR_CMD_SEQ_DIAG_LCD),
		      EC_RES_SUCCESS);
	zassert_equal(lb_set_diag_policy_fake.call_count, 1);
	zassert_equal(lb_set_diag_policy_fake.arg0_val,
		      LED_ALT_POLICY_DIAG_LCD);
	zassert_equal(lb_set_diag_policy_fake.arg1_val, 600000);

	RESET_FAKE(lb_set_diag_policy);
	zassert_equal(board_lightbar_custom_seq(0xff), EC_RES_INVALID_PARAM);
	zassert_equal(lb_set_diag_policy_fake.call_count, 0);
}

/* Test LED behavior during power failure and clearance */
ZTEST(mica_led, test_power_fail_and_clear)
{
	board_diag_led_power_fail();
	zassert_equal(lb_set_diag_policy_fake.call_count, 1);
	zassert_equal(lb_set_diag_policy_fake.arg0_val,
		      LED_ALT_POLICY_DIAG_PWR);
	zassert_equal(lb_set_diag_policy_fake.arg1_val, 600000);

	RESET_FAKE(lb_set_diag_policy);
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(lb_set_diag_policy_fake.call_count, 1);
	zassert_equal(lb_set_diag_policy_fake.arg0_val, LED_ALT_POLICY_NORMAL);
	zassert_equal(lb_set_diag_policy_fake.arg1_val, 0);

	/* Subsequent startup should not trigger clear again */
	RESET_FAKE(lb_set_diag_policy);
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(lb_set_diag_policy_fake.call_count, 0);
}
