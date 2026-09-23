/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/lightbar_policy_alt.h"
#include "lb_policy.h"
#include "led_lb_host_program.h"
#include "led_lightbar.h"
#include "stubs.h"

#include <zephyr/ztest.h>

enum ec_status board_lightbar_custom_seq(uint8_t seq);

static void test_led_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(lb_set_diag_policy);
}

ZTEST_SUITE(quartz_led, NULL, NULL, test_led_before, NULL, NULL);

/* Test custom lightbar animation sequence for ramdump */
ZTEST(quartz_led, test_lightbar_custom_seq)
{
	/* Valid sequence */
	zassert_equal(board_lightbar_custom_seq(LIGHTBAR_CMD_SEQ_RAMDUMP),
		      EC_RES_SUCCESS);
	zassert_equal(lb_set_diag_policy_fake.call_count, 1);
	zassert_equal(lb_set_diag_policy_fake.arg0_val,
		      LED_ALT_POLICY_DIAG_RAMDUMP);
	zassert_equal(lb_set_diag_policy_fake.arg1_val, 90000);

	/* Invalid sequence */
	RESET_FAKE(lb_set_diag_policy);
	zassert_equal(board_lightbar_custom_seq(0xff), EC_RES_INVALID_PARAM);
	zassert_equal(lb_set_diag_policy_fake.call_count, 0);
}
